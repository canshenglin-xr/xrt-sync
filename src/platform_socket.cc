// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN

#include "platform_socket.h"

#include <atomic>
#include <cstring>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
   using socklen_t_compat = int;
#  define XS_CLOSESOCKET closesocket
#  define XS_LAST_ERROR (WSAGetLastError())
#  define XS_WOULDBLOCK WSAEWOULDBLOCK
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
   using socklen_t_compat = socklen_t;
#  define XS_CLOSESOCKET ::close
#  define XS_LAST_ERROR (errno)
#  define XS_WOULDBLOCK EAGAIN
#endif

namespace xrtsync::platform {

namespace {

std::atomic<int> g_init_refcount{0};

bool SetNonblocking(int fd) {
#if defined(_WIN32)
  u_long mode = 1;
  return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode) == 0;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

}  // namespace

bool InitializeNetworking() {
  if (g_init_refcount.fetch_add(1) > 0) return true;
#if defined(_WIN32)
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  return true;
#endif
}

void ShutdownNetworking() {
  if (g_init_refcount.fetch_sub(1) > 1) return;
#if defined(_WIN32)
  WSACleanup();
#endif
}

bool Endpoint::operator==(const Endpoint& o) const noexcept {
  return length == o.length && std::memcmp(storage, o.storage, length) == 0;
}

std::string Endpoint::ToString() const {
  char host[64] = {0};
  char port[16] = {0};
  if (getnameinfo(reinterpret_cast<const sockaddr*>(storage),
                  static_cast<socklen_t_compat>(length),
                  host, sizeof(host), port, sizeof(port),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    return "<invalid>";
  }
  // Bracketize IPv6 so the round-trip through ParseEndpoint is lossless.
  if (std::strchr(host, ':') != nullptr) {
    return std::string("[") + host + "]:" + port;
  }
  return std::string(host) + ":" + port;
}

std::optional<Endpoint> ParseEndpoint(std::string_view spec) {
  // We need a null-terminated copy because getaddrinfo wants C strings.
  std::string s(spec);
  std::string host;
  std::string port;
  if (!s.empty() && s.front() == '[') {
    auto rb = s.find(']');
    if (rb == std::string::npos || rb + 2 > s.size() || s[rb + 1] != ':') {
      return std::nullopt;
    }
    host = s.substr(1, rb - 1);
    port = s.substr(rb + 2);
  } else {
    auto colon = s.rfind(':');
    if (colon == std::string::npos) return std::nullopt;
    host = s.substr(0, colon);
    port = s.substr(colon + 1);
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;

  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 ||
      res == nullptr) {
    return std::nullopt;
  }

  Endpoint ep;
  ep.length = res->ai_addrlen;
  if (ep.length > sizeof(ep.storage)) {
    freeaddrinfo(res);
    return std::nullopt;
  }
  std::memcpy(ep.storage, res->ai_addr, ep.length);
  freeaddrinfo(res);
  return ep;
}

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket() {
  if (fd_ >= 0) XS_CLOSESOCKET(fd_);
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) XS_CLOSESOCKET(fd_);
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

bool UdpSocket::Bind(const Endpoint& local) {
  const sockaddr* sa = reinterpret_cast<const sockaddr*>(local.storage);
  int family = sa->sa_family;
  int fd = static_cast<int>(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (fd < 0) return false;
  // Reuse address so back-to-back restarts during development are smooth.
  int yes = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));
  if (::bind(fd, sa, static_cast<socklen_t_compat>(local.length)) != 0) {
    XS_CLOSESOCKET(fd);
    return false;
  }
  if (!SetNonblocking(fd)) {
    XS_CLOSESOCKET(fd);
    return false;
  }
  if (fd_ >= 0) XS_CLOSESOCKET(fd_);
  fd_ = fd;
  return true;
}

bool UdpSocket::OpenEphemeral(bool ipv6) {
  int family = ipv6 ? AF_INET6 : AF_INET;
  int fd = static_cast<int>(::socket(family, SOCK_DGRAM, IPPROTO_UDP));
  if (fd < 0) return false;
  if (!SetNonblocking(fd)) {
    XS_CLOSESOCKET(fd);
    return false;
  }
  if (fd_ >= 0) XS_CLOSESOCKET(fd_);
  fd_ = fd;
  return true;
}

long UdpSocket::Send(const Endpoint& dst, const void* data, std::size_t size) {
  if (fd_ < 0) return -1;
  const sockaddr* sa = reinterpret_cast<const sockaddr*>(dst.storage);
#if defined(_WIN32)
  int n = ::sendto(fd_, static_cast<const char*>(data),
                   static_cast<int>(size), 0, sa,
                   static_cast<socklen_t_compat>(dst.length));
#else
  ssize_t n = ::sendto(fd_, data, size, 0, sa,
                       static_cast<socklen_t_compat>(dst.length));
#endif
  return static_cast<long>(n);
}

long UdpSocket::ReceiveWithTimeout(void* buffer, std::size_t capacity,
                                   Endpoint* out_src,
                                   std::uint32_t timeout_us) {
  if (fd_ < 0) return -1;

#if defined(_WIN32)
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(static_cast<SOCKET>(fd_), &readfds);
  timeval tv;
  tv.tv_sec = static_cast<long>(timeout_us / 1'000'000);
  tv.tv_usec = static_cast<long>(timeout_us % 1'000'000);
  int sel = ::select(0, &readfds, nullptr, nullptr,
                     timeout_us == 0 ? &tv : &tv);
  if (sel <= 0) return sel;  // 0 = timeout, -1 = error
#else
  pollfd p{};
  p.fd = fd_;
  p.events = POLLIN;
  int sel = ::poll(&p, 1, static_cast<int>(timeout_us / 1000));
  if (sel <= 0) return sel;
#endif

  socklen_t_compat src_len = static_cast<socklen_t_compat>(sizeof(out_src->storage));
#if defined(_WIN32)
  int n = ::recvfrom(fd_, static_cast<char*>(buffer),
                     static_cast<int>(capacity), 0,
                     reinterpret_cast<sockaddr*>(out_src->storage), &src_len);
#else
  ssize_t n = ::recvfrom(fd_, buffer, capacity, 0,
                         reinterpret_cast<sockaddr*>(out_src->storage),
                         &src_len);
#endif
  if (n < 0) {
    if (XS_LAST_ERROR == XS_WOULDBLOCK) return 0;
    return -1;
  }
  out_src->length = src_len;
  return static_cast<long>(n);
}

}  // namespace xrtsync::platform
