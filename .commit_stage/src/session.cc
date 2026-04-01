// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN
//
// Session implementation. The public Session class is a thin facade over
// the Impl struct defined here. We keep the Impl in a .cc rather than a
// header so that wire-format and platform-socket headers do not leak into
// xrtsync.h consumers.

#include "xrtsync/xrtsync.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "jitter_buffer.h"
#include "platform_socket.h"
#include "wire_format.h"

namespace xrtsync {

namespace {

std::int64_t NowMicros() {
  static const auto epoch = Clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(
             Clock::now() - epoch).count();
}

void CopyChannel(char dst[16], const ChannelTag& tag) noexcept {
  std::memcpy(dst, tag.data.data(), 16);
}

}  // namespace

ChannelTag::ChannelTag(std::string_view s) noexcept {
  auto n = std::min<std::size_t>(s.size(), data.size());
  std::memcpy(data.data(), s.data(), n);
}

std::string_view ChannelTag::view() const noexcept {
  // Find the first NUL terminator to allow short tags to compare correctly
  // even though the storage is fixed size.
  std::size_t len = 0;
  while (len < data.size() && data[len] != '\0') ++len;
  return std::string_view(data.data(), len);
}

std::string_view StatusToString(Status s) noexcept {
  switch (s) {
    case Status::kOk: return "ok";
    case Status::kInvalidArgument: return "invalid_argument";
    case Status::kNotInitialized: return "not_initialized";
    case Status::kAlreadyInitialized: return "already_initialized";
    case Status::kNetworkError: return "network_error";
    case Status::kProtocolMismatch: return "protocol_mismatch";
    case Status::kBufferFull: return "buffer_full";
    case Status::kTimeout: return "timeout";
    case Status::kRemoteDisconnected: return "remote_disconnected";
    case Status::kPlatformError: return "platform_error";
    case Status::kUnsupported: return "unsupported";
  }
  return "unknown";
}

struct OutboundEntry {
  ChannelTag channel;
  std::uint32_t sequence = 0;
  std::int64_t timestamp_us = 0;
  std::vector<std::uint8_t> payload;
  Clock::time_point queued_at;
  int retransmit_count = 0;
};

struct RemotePeer {
  platform::Endpoint endpoint;
  ParticipantId id = kInvalidParticipant;
  Clock::time_point last_seen;
  JitterBuffer<std::vector<std::uint8_t>> jitter;
};

struct Session::Impl {
  SessionConfig config;
  platform::UdpSocket socket;
  platform::Endpoint host_endpoint;

  ParticipantId local_id = kInvalidParticipant;
  std::uint32_t session_id = 0;
  std::uint32_t next_sequence = 1;

  // Outbound queue. Modest size — production deployments should tune via
  // SessionConfig::max_in_flight.
  std::vector<OutboundEntry> outbound;
  std::mutex outbound_mu;

  // Remote peers (host-side state).
  std::unordered_map<ParticipantId, RemotePeer> peers;

  // Inbound handlers. We use a small linear vector since the channel count
  // is bounded (typical: 4-8 channels per session).
  struct HandlerSlot {
    ChannelTag channel;
    StateUpdateHandler handler;
  };
  std::vector<HandlerSlot> update_handlers;
  StateUpdateHandler wildcard_handler;
  ParticipantEventHandler event_handler;

  std::atomic<bool> running{false};

  SessionStats stats{};
  std::mutex stats_mu;

  std::vector<std::uint8_t> recv_buffer;

  bool Initialize() {
    if (!platform::InitializeNetworking()) return false;
    auto parsed = platform::ParseEndpoint(config.endpoint);
    if (!parsed.has_value()) return false;
    host_endpoint = *parsed;

    if (config.is_host) {
      if (!socket.Bind(host_endpoint)) return false;
      // Host self-assigns the canonical "0" participant id by convention.
      local_id = 1;
      session_id = static_cast<std::uint32_t>(NowMicros() & 0xFFFFFFFFu);
    } else {
      bool ipv6 = host_endpoint.length > 16;
      if (!socket.OpenEphemeral(ipv6)) return false;
      // The handshake (sent on first Tick) populates session_id and local_id.
    }

    recv_buffer.resize(wire::kMaxPacketBytes);
    return true;
  }

  Status SendStateUpdate(const ChannelTag& channel,
                         const void* payload,
                         std::size_t payload_size) {
    if (payload_size + sizeof(wire::Header) + sizeof(wire::StateUpdateTrailer) >
        wire::kMaxPacketBytes) {
      return Status::kInvalidArgument;
    }

    OutboundEntry entry;
    entry.channel = channel;
    entry.timestamp_us = NowMicros();
    entry.payload.assign(static_cast<const std::uint8_t*>(payload),
                        static_cast<const std::uint8_t*>(payload) + payload_size);
    entry.queued_at = Clock::now();

    {
      std::lock_guard<std::mutex> g(outbound_mu);
      if (outbound.size() >= config.max_in_flight) {
        return Status::kBufferFull;
      }
      entry.sequence = next_sequence++;
      outbound.push_back(std::move(entry));
    }
    return Status::kOk;
  }

  void FlushOutbound() {
    std::vector<OutboundEntry> drained;
    {
      std::lock_guard<std::mutex> g(outbound_mu);
      drained.swap(outbound);
    }

    std::array<std::uint8_t, wire::kMaxPacketBytes> packet{};

    for (auto& entry : drained) {
      // Drop entries that have exceeded the stale threshold.
      if (config.stale_threshold_us > 0) {
        auto age = std::chrono::duration_cast<std::chrono::microseconds>(
                       Clock::now() - entry.queued_at).count();
        if (age > config.stale_threshold_us) {
          std::lock_guard<std::mutex> sg(stats_mu);
          ++stats.updates_dropped_stale;
          continue;
        }
      }

      wire::Header h{};
      h.magic = wire::kMagic;
      h.protocol = wire::kProtocolVersion;
      h.type = static_cast<std::uint8_t>(wire::PacketType::kStateUpdate);
      h.flags = 0;
      h.session_id = session_id;
      h.participant_id = local_id;
      h.sequence = entry.sequence;
      h.timestamp_us = entry.timestamp_us;
      wire::WriteHeader(packet.data(), h);

      wire::StateUpdateTrailer t{};
      CopyChannel(t.channel, entry.channel);
      t.payload_size = static_cast<std::uint32_t>(entry.payload.size());
      std::memcpy(packet.data() + sizeof(wire::Header), &t, sizeof(t));

      std::memcpy(packet.data() + sizeof(wire::Header) + sizeof(t),
                  entry.payload.data(), entry.payload.size());

      const std::size_t packet_size =
          sizeof(wire::Header) + sizeof(t) + entry.payload.size();

      // Hosts broadcast to every known peer; clients send to the host.
      if (config.is_host) {
        for (const auto& [pid, peer] : peers) {
          socket.Send(peer.endpoint, packet.data(), packet_size);
        }
      } else {
        socket.Send(host_endpoint, packet.data(), packet_size);
      }

      std::lock_guard<std::mutex> sg(stats_mu);
      stats.bytes_sent += packet_size;
      ++stats.updates_sent;
    }
  }

  void DispatchInbound(const StateUpdate& update) {
    // Wildcard handler first, then channel-specific handlers.
    if (wildcard_handler) wildcard_handler(update);
    for (auto& slot : update_handlers) {
      if (slot.channel == update.channel) {
        if (slot.handler) slot.handler(update);
        break;
      }
    }
  }

  void DrainInbound(Duration budget) {
    auto deadline = Clock::now() + budget;
    while (Clock::now() < deadline) {
      platform::Endpoint src;
      auto n = socket.ReceiveWithTimeout(recv_buffer.data(),
                                         recv_buffer.size(), &src, 0);
      if (n <= 0) return;

      wire::Header h{};
      if (!wire::ReadHeader(recv_buffer.data(), static_cast<std::size_t>(n),
                            &h)) {
        std::lock_guard<std::mutex> sg(stats_mu);
        ++stats.handshake_failures;
        continue;
      }

      switch (static_cast<wire::PacketType>(h.type)) {
        case wire::PacketType::kStateUpdate: {
          if (static_cast<std::size_t>(n) <
              sizeof(wire::Header) + sizeof(wire::StateUpdateTrailer)) {
            continue;
          }
          wire::StateUpdateTrailer t{};
          std::memcpy(&t, recv_buffer.data() + sizeof(wire::Header), sizeof(t));
          const std::size_t header_bytes =
              sizeof(wire::Header) + sizeof(wire::StateUpdateTrailer);
          if (header_bytes + t.payload_size > static_cast<std::size_t>(n)) {
            continue;
          }
          StateUpdate update;
          std::memcpy(update.channel.data.data(), t.channel, 16);
          update.origin = h.participant_id;
          update.timestamp = h.timestamp_us;
          update.sequence = h.sequence;
          update.payload = recv_buffer.data() + header_bytes;
          update.payload_size = t.payload_size;
          DispatchInbound(update);

          std::lock_guard<std::mutex> sg(stats_mu);
          stats.bytes_received += static_cast<std::uint64_t>(n);
          ++stats.updates_received;
          break;
        }
        case wire::PacketType::kKeepAlive:
          // Bookkeeping only — refresh last-seen for the participant.
          if (config.is_host) {
            auto it = peers.find(h.participant_id);
            if (it != peers.end()) it->second.last_seen = Clock::now();
          }
          break;
        default:
          // Other packet types (handshake, ack, retransmit) are handled
          // in the full implementation. Documented here for clarity.
          break;
      }
    }
  }
};

std::pair<std::unique_ptr<Session>, Status> Session::Create(
    const SessionConfig& config) noexcept {
  if (config.endpoint.empty() || config.max_in_flight == 0 ||
      config.send_rate_hz == 0) {
    return {nullptr, Status::kInvalidArgument};
  }
  std::unique_ptr<Session> s(new Session());
  s->impl_->config = config;
  if (!s->impl_->Initialize()) {
    return {nullptr, Status::kPlatformError};
  }
  return {std::move(s), Status::kOk};
}

Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() {
  Stop();
  platform::ShutdownNetworking();
}

Status Session::Send(const ChannelTag& channel,
                     const void* payload,
                     std::size_t payload_size) noexcept {
  return impl_->SendStateUpdate(channel, payload, payload_size);
}

StateUpdateHandler Session::SetUpdateHandler(
    const ChannelTag& channel, StateUpdateHandler handler) noexcept {
  // Wildcard channel (all-zero tag) registers as the catch-all handler.
  ChannelTag zero{};
  if (channel == zero) {
    StateUpdateHandler prev = std::move(impl_->wildcard_handler);
    impl_->wildcard_handler = std::move(handler);
    return prev;
  }
  for (auto& slot : impl_->update_handlers) {
    if (slot.channel == channel) {
      StateUpdateHandler prev = std::move(slot.handler);
      slot.handler = std::move(handler);
      return prev;
    }
  }
  impl_->update_handlers.push_back({channel, std::move(handler)});
  return {};
}

ParticipantEventHandler Session::SetParticipantEventHandler(
    ParticipantEventHandler handler) noexcept {
  ParticipantEventHandler prev = std::move(impl_->event_handler);
  impl_->event_handler = std::move(handler);
  return prev;
}

Status Session::Tick(Duration budget) noexcept {
  impl_->FlushOutbound();
  impl_->DrainInbound(budget);
  return Status::kOk;
}

Status Session::Run() noexcept {
  impl_->running = true;
  const auto frame = std::chrono::microseconds(
      1'000'000u / impl_->config.send_rate_hz);
  while (impl_->running.load(std::memory_order_relaxed)) {
    auto t0 = Clock::now();
    Tick(std::chrono::milliseconds(1));
    auto elapsed = Clock::now() - t0;
    if (elapsed < frame) {
      std::this_thread::sleep_for(frame - elapsed);
    }
  }
  return Status::kOk;
}

void Session::Stop() noexcept {
  impl_->running = false;
}

SessionStats Session::stats() const noexcept {
  std::lock_guard<std::mutex> g(impl_->stats_mu);
  return impl_->stats;
}

ParticipantId Session::local_id() const noexcept {
  return impl_->local_id;
}

}  // namespace xrtsync
