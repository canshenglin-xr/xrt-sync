// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN
//
// Cross-platform thin wrapper over UDP sockets. We deliberately keep this
// in a small set of files rather than reaching for a heavyweight
// dependency like Boost.Asio — the entire surface we need is one socket,
// nonblocking I/O, and a coarse timeout.
//
// Supported platforms:
//   * Windows  (Winsock2)
//   * macOS    (BSD sockets)
//   * Linux    (BSD sockets)
//   * iOS      (BSD sockets; same path as macOS)
//   * Android  (BSD sockets; same path as Linux)

#ifndef XRTSYNC_SRC_PLATFORM_SOCKET_H_
#define XRTSYNC_SRC_PLATFORM_SOCKET_H_

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace xrtsync::platform {

// Initialize platform networking. On Windows this calls WSAStartup; on
// every other platform it is a no-op. Safe to call multiple times.
bool InitializeNetworking();

// Tear down platform networking. Reference counted with
// InitializeNetworking().
void ShutdownNetworking();

struct Endpoint {
  // Storage sized for sockaddr_in6 to avoid a heap allocation per packet.
  std::uint8_t storage[28] = {};
  std::size_t length = 0;

  bool operator==(const Endpoint& o) const noexcept;
  std::string ToString() const;
};

// Parse "host:port" into an Endpoint. Supports IPv4 (1.2.3.4:5000) and
// IPv6 ([::1]:5000) literals. Returns std::nullopt on parse failure.
std::optional<Endpoint> ParseEndpoint(std::string_view spec);

class UdpSocket {
 public:
  UdpSocket();
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  UdpSocket(UdpSocket&& other) noexcept;
  UdpSocket& operator=(UdpSocket&& other) noexcept;

  // Bind to a local endpoint and switch to nonblocking mode. Used by hosts
  // and by clients that want a deterministic local port.
  bool Bind(const Endpoint& local);

  // Open an ephemeral local port; used by clients.
  bool OpenEphemeral(bool ipv6);

  // Send a packet. Returns the number of bytes sent, or -1 on error.
  long Send(const Endpoint& dst, const void* data, std::size_t size);

  // Try to receive a packet. Returns the number of bytes received, 0 if no
  // packet is available within `timeout_us`, or -1 on error.
  long ReceiveWithTimeout(void* buffer, std::size_t capacity,
                          Endpoint* out_src, std::uint32_t timeout_us);

  bool IsOpen() const noexcept { return fd_ >= 0; }

 private:
  int fd_ = -1;
};

}  // namespace xrtsync::platform

#endif  // XRTSYNC_SRC_PLATFORM_SOCKET_H_
