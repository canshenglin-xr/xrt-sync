// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN
//
// Adaptive jitter buffer with deterministic recovery for high-frequency
// state streams (AR/XR pose updates, input events). The buffer trades a
// configurable amount of latency for protection against out-of-order
// delivery and short bursts of packet loss.
//
// Design notes (see docs/ARCHITECTURE.md, §3 "Jitter handling"):
//
//   * We model the network's one-way delay as a slowly varying random
//     variable. The buffer maintains an exponentially weighted moving
//     average of inter-arrival time and uses 4 * sigma as the playout
//     deadline target — this gives ~99.99% on-time delivery for Gaussian
//     jitter.
//
//   * On detected loss (sequence gap), we wait up to one RTT for the
//     missing packet before declaring it lost and emitting an interpolated
//     placeholder so that downstream consumers (e.g., the AR rendering
//     loop) never stall.
//
//   * The buffer is parameterized on payload type so the same code path
//     serves byte-blob payloads, structured pose samples, and arbitrary
//     user-defined types.

#ifndef XRTSYNC_SRC_JITTER_BUFFER_H_
#define XRTSYNC_SRC_JITTER_BUFFER_H_

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <optional>
#include <vector>

namespace xrtsync {

template <typename Payload>
class JitterBuffer {
 public:
  struct Entry {
    std::uint32_t sequence = 0;
    std::int64_t timestamp_us = 0;
    Payload payload{};
    std::chrono::steady_clock::time_point arrived_at;
    bool is_placeholder = false;
  };

  // Configure the playout deadline floor and ceiling. The adaptive
  // algorithm operates inside [min_delay, max_delay].
  void Configure(std::chrono::microseconds min_delay,
                 std::chrono::microseconds max_delay) noexcept {
    min_delay_ = min_delay;
    max_delay_ = max_delay;
    current_delay_ = min_delay;
  }

  // Insert a freshly arrived packet. Out-of-order arrivals are placed at
  // the correct slot; duplicates are silently dropped.
  void Insert(std::uint32_t sequence,
              std::int64_t timestamp_us,
              Payload payload) noexcept {
    auto now = std::chrono::steady_clock::now();

    // Update arrival statistics. We compute the inter-arrival interval as
    // the wall delta between this packet and the most recently arrived
    // packet, regardless of sequence. The EWMA smooths over short bursts.
    if (last_arrival_.time_since_epoch().count() != 0) {
      auto delta = now - last_arrival_;
      auto delta_us = std::chrono::duration_cast<std::chrono::microseconds>(
          delta).count();
      if (smoothed_interval_us_ == 0) {
        smoothed_interval_us_ = delta_us;
      } else {
        // alpha = 1/8, standard RFC 6298-style smoothing.
        smoothed_interval_us_ =
            (smoothed_interval_us_ * 7 + delta_us) / 8;
        std::int64_t deviation =
            std::abs(delta_us - smoothed_interval_us_);
        smoothed_jitter_us_ =
            (smoothed_jitter_us_ * 3 + deviation) / 4;
      }
    }
    last_arrival_ = now;

    // Adaptive delay: 4 * jitter, clamped to the configured envelope.
    auto adaptive_us = smoothed_jitter_us_ * 4;
    if (adaptive_us < min_delay_.count()) adaptive_us = min_delay_.count();
    if (adaptive_us > max_delay_.count()) adaptive_us = max_delay_.count();
    current_delay_ = std::chrono::microseconds(adaptive_us);

    // Drop packets older than the most recently played sequence.
    if (last_played_.has_value() && SeqLessOrEqual(sequence, *last_played_)) {
      return;
    }

    Entry entry{sequence, timestamp_us, std::move(payload), now, false};

    // Insert in sequence order. The buffer is small (bounded by current
    // delay * arrival rate), so linear insertion is fine.
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->sequence == sequence) return;  // duplicate
      if (SeqLess(sequence, it->sequence)) {
        entries_.insert(it, std::move(entry));
        return;
      }
    }
    entries_.push_back(std::move(entry));
  }

  // Pop the next entry whose playout deadline has elapsed. Returns
  // std::nullopt if no entry is ready yet.
  std::optional<Entry> Pop() noexcept {
    if (entries_.empty()) return std::nullopt;
    auto now = std::chrono::steady_clock::now();
    auto& front = entries_.front();
    auto playout_at = front.arrived_at + current_delay_;
    if (now < playout_at) return std::nullopt;

    // Detect a sequence gap at the head. If the gap is small and recent,
    // wait briefly for the missing packet to arrive. Otherwise, emit a
    // placeholder for the missing sequence so downstream consumers can
    // interpolate without stalling.
    if (last_played_.has_value()) {
      std::uint32_t expected = *last_played_ + 1;
      if (front.sequence != expected) {
        auto wait_budget = current_delay_;
        if (now - front.arrived_at < wait_budget &&
            SeqDistance(expected, front.sequence) <= 4) {
          return std::nullopt;
        }
        // Emit placeholder for the lost packet. The caller is expected to
        // interpolate or repeat the prior payload.
        Entry placeholder{};
        placeholder.sequence = expected;
        placeholder.timestamp_us = front.timestamp_us;
        placeholder.is_placeholder = true;
        placeholder.arrived_at = now;
        last_played_ = expected;
        return placeholder;
      }
    }

    Entry out = std::move(entries_.front());
    entries_.pop_front();
    last_played_ = out.sequence;
    return out;
  }

  std::size_t size() const noexcept { return entries_.size(); }
  std::chrono::microseconds current_delay() const noexcept { return current_delay_; }
  std::int64_t smoothed_jitter_us() const noexcept { return smoothed_jitter_us_; }

 private:
  // Sequence-space comparisons that handle 32-bit wraparound. Two
  // sequences are compared by their signed 32-bit difference.
  static bool SeqLess(std::uint32_t a, std::uint32_t b) noexcept {
    return static_cast<std::int32_t>(a - b) < 0;
  }
  static bool SeqLessOrEqual(std::uint32_t a, std::uint32_t b) noexcept {
    return static_cast<std::int32_t>(a - b) <= 0;
  }
  static std::uint32_t SeqDistance(std::uint32_t a, std::uint32_t b) noexcept {
    std::int32_t d = static_cast<std::int32_t>(b - a);
    return d < 0 ? static_cast<std::uint32_t>(-d) : static_cast<std::uint32_t>(d);
  }

  std::deque<Entry> entries_;
  std::optional<std::uint32_t> last_played_;
  std::chrono::steady_clock::time_point last_arrival_;
  std::int64_t smoothed_interval_us_ = 0;
  std::int64_t smoothed_jitter_us_ = 0;
  std::chrono::microseconds min_delay_{2000};
  std::chrono::microseconds max_delay_{32000};
  std::chrono::microseconds current_delay_{2000};
};

}  // namespace xrtsync

#endif  // XRTSYNC_SRC_JITTER_BUFFER_H_
