// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN <canshenglin@github>
//
// xrt-sync: Cross-platform real-time state synchronization middleware
// for immersive computing, AR/XR engineering integration, and large-scale
// interactive systems.
//
// Public C++17 API. Header-only for the public surface; the implementation
// lives in src/ and is compiled into a static or shared library.
//
// Design goals (see docs/ARCHITECTURE.md):
//   1. Platform-agnostic: Windows, macOS, Linux, iOS, Android.
//   2. Sub-frame latency for AR/XR pose streams (target p99 < 16 ms).
//   3. Deterministic recovery from packet loss and network jitter.
//   4. Zero external runtime dependencies; only the C++17 standard library
//      plus the platform's native sockets API.
//   5. Embeddable into Unity / Unreal / WebXR runtimes through a stable
//      C ABI (see include/xrtsync/c_abi.h).

#ifndef XRTSYNC_XRTSYNC_H_
#define XRTSYNC_XRTSYNC_H_

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xrtsync {

// Library version. Updated by tools/bump_version.py.
constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 3;
constexpr int kVersionPatch = 1;

// Result codes returned by all fallible APIs. We deliberately avoid throwing
// exceptions from the public surface — engine integrations (Unity / Unreal)
// generally compile with -fno-exceptions.
enum class Status : std::uint8_t {
  kOk = 0,
  kInvalidArgument,
  kNotInitialized,
  kAlreadyInitialized,
  kNetworkError,
  kProtocolMismatch,
  kBufferFull,
  kTimeout,
  kRemoteDisconnected,
  kPlatformError,
  kUnsupported,
};

// Human-readable description for a Status. Useful for logging.
std::string_view StatusToString(Status s) noexcept;

// Monotonic clock used everywhere in xrt-sync. We use steady_clock so that
// timestamps are monotonic and unaffected by wall-clock jumps.
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

// 64-bit logical timestamp expressed in microseconds since the session
// origin. Sent on the wire; preserved across all platforms.
using LogicalUs = std::int64_t;

// Stable identifier for a participant in a synchronization session. Assigned
// by the session host during the handshake.
using ParticipantId = std::uint32_t;
constexpr ParticipantId kInvalidParticipant = 0;

// A short opaque tag (up to 16 bytes) used to label a state channel. Common
// channels in AR/XR workloads: "pose", "input", "voice-meta", "scene-delta".
struct ChannelTag {
  std::array<char, 16> data{};
  constexpr ChannelTag() = default;
  explicit ChannelTag(std::string_view s) noexcept;
  std::string_view view() const noexcept;
  bool operator==(const ChannelTag& o) const noexcept { return data == o.data; }
};

// A single immutable state update. The payload is owned by the caller for
// the duration of the call; xrt-sync copies it internally if it needs to
// buffer the message for retransmission.
struct StateUpdate {
  ChannelTag channel;
  ParticipantId origin = kInvalidParticipant;
  LogicalUs timestamp = 0;
  std::uint32_t sequence = 0;
  const void* payload = nullptr;
  std::size_t payload_size = 0;
};

// Configuration for a synchronization session.
struct SessionConfig {
  // UDP endpoint used by the session host. Format: "host:port" (IPv4 or
  // IPv6). When acting as a client, this is the host to connect to. When
  // acting as a host, this is the bind address.
  std::string endpoint;

  // Soft cap on the number of in-flight unacknowledged updates per channel.
  // Lower values reduce memory at the cost of throughput; higher values
  // hide more jitter at the cost of recovery latency.
  std::uint32_t max_in_flight = 256;

  // Target send rate in updates per second per channel. The scheduler
  // smooths bursts to honor this rate.
  std::uint32_t send_rate_hz = 120;

  // If non-zero, drop updates older than this many microseconds when the
  // outbound queue is congested. AR/XR pose channels usually set this to
  // ~16,000 (one display frame at 60 Hz); voice metadata channels disable
  // this by leaving it at zero.
  std::uint32_t stale_threshold_us = 0;

  // Whether this endpoint acts as the session host. Hosts assign
  // ParticipantIds and serve as the canonical clock source.
  bool is_host = false;

  // Optional pre-shared key for the handshake. Empty string disables
  // authentication. Production deployments should always set a key.
  std::string preshared_key;
};

// Statistics snapshot, refreshed on every call to Session::stats(). Cheap to
// query — designed for per-frame HUD overlays during development.
struct SessionStats {
  std::uint64_t bytes_sent = 0;
  std::uint64_t bytes_received = 0;
  std::uint64_t updates_sent = 0;
  std::uint64_t updates_received = 0;
  std::uint64_t updates_dropped_stale = 0;
  std::uint64_t retransmissions = 0;
  std::uint64_t handshake_failures = 0;

  // Smoothed one-way latency from this peer's send to remote ack.
  Duration smoothed_rtt = Duration::zero();

  // Smoothed jitter (mean absolute deviation of inter-arrival time).
  Duration smoothed_jitter = Duration::zero();

  // Count of active remote participants seen in the last second.
  std::uint32_t active_participants = 0;
};

// Callback signature for inbound state updates. The pointer in `update`
// references an internal buffer that remains valid only for the duration of
// the callback — copy it if you need to retain the payload.
using StateUpdateHandler =
    std::function<void(const StateUpdate& update) noexcept>;

// Callback signature for participant lifecycle events.
enum class ParticipantEvent : std::uint8_t {
  kJoined = 0,
  kLeft = 1,
  kTimedOut = 2,
};
using ParticipantEventHandler =
    std::function<void(ParticipantId who, ParticipantEvent event) noexcept>;

// A Session represents one connected synchronization endpoint. Sessions are
// created via Session::Create(); the constructor is private to enforce the
// Status-returning factory pattern.
class Session {
 public:
  static std::pair<std::unique_ptr<Session>, Status> Create(
      const SessionConfig& config) noexcept;

  ~Session();

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Queue an outbound state update on `channel`. Returns kOk if the update
  // was accepted into the outbound queue; kBufferFull if the queue is
  // saturated and the caller should back off.
  Status Send(const ChannelTag& channel,
              const void* payload,
              std::size_t payload_size) noexcept;

  // Register a handler for inbound updates on `channel`. Passing an empty
  // ChannelTag registers a wildcard handler that receives all channels.
  // Returns the previous handler so callers can chain.
  StateUpdateHandler SetUpdateHandler(const ChannelTag& channel,
                                      StateUpdateHandler handler) noexcept;

  // Register a handler for participant lifecycle events.
  ParticipantEventHandler SetParticipantEventHandler(
      ParticipantEventHandler handler) noexcept;

  // Run one tick of the session's internal scheduler. Most engines call
  // this once per frame on the main thread; standalone applications can
  // call Run() instead to spin the internal loop on a worker thread.
  Status Tick(Duration budget = std::chrono::milliseconds(2)) noexcept;

  // Run the internal loop until Stop() is called. Spawns no threads of its
  // own; the caller controls the thread.
  Status Run() noexcept;

  // Request an in-progress Run() to exit. Safe to call from any thread.
  void Stop() noexcept;

  // Current statistics.
  SessionStats stats() const noexcept;

  // This endpoint's assigned ParticipantId (set during handshake).
  ParticipantId local_id() const noexcept;

 private:
  Session();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xrtsync

#endif  // XRTSYNC_XRTSYNC_H_
