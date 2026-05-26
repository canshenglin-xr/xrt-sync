// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN
//
// Internal wire format for xrt-sync UDP packets. Versioned, little-endian,
// designed to be stable across the 0.x release line. See
// docs/PROTOCOL.md for the full specification.

#ifndef XRTSYNC_SRC_WIRE_FORMAT_H_
#define XRTSYNC_SRC_WIRE_FORMAT_H_

#include <cstdint>
#include <cstring>

namespace xrtsync::wire {

constexpr std::uint32_t kMagic = 0x58525453;  // "XRTS"
constexpr std::uint16_t kProtocolVersion = 0x0001;

// Maximum UDP payload size we will emit. Conservative cap that fits inside
// the typical 1500-byte path MTU even after IPv6 + UDP headers and
// reasonable tunnel overhead.
constexpr std::size_t kMaxPacketBytes = 1200;

enum class PacketType : std::uint8_t {
  kHandshakeRequest = 1,
  kHandshakeAccept = 2,
  kHandshakeReject = 3,
  kStateUpdate = 10,
  kStateAck = 11,
  kRetransmitRequest = 12,
  kKeepAlive = 20,
  kParticipantList = 30,
};

// Common header prefix on every packet. Field order is fixed for the
// lifetime of the protocol; new fields are added by introducing new
// PacketTypes with their own trailing layout.
#pragma pack(push, 1)
struct Header {
  std::uint32_t magic;          // kMagic
  std::uint16_t protocol;       // kProtocolVersion
  std::uint8_t  type;           // PacketType
  std::uint8_t  flags;          // reserved for future use
  std::uint32_t session_id;     // assigned by host
  std::uint32_t participant_id; // sender's ParticipantId
  std::uint32_t sequence;       // sender-monotonic
  std::int64_t  timestamp_us;   // sender's LogicalUs
};
static_assert(sizeof(Header) == 28, "Header layout is part of the wire ABI");

// Trailing layout for kStateUpdate. Followed by payload_size bytes of
// opaque payload.
struct StateUpdateTrailer {
  char channel[16];
  std::uint32_t payload_size;
};
static_assert(sizeof(StateUpdateTrailer) == 20,
              "StateUpdateTrailer layout is part of the wire ABI");

// Trailing layout for kStateAck.
struct StateAckTrailer {
  char channel[16];
  std::uint32_t acked_sequence;
  std::int64_t recv_timestamp_us;
};
static_assert(sizeof(StateAckTrailer) == 28,
              "StateAckTrailer layout is part of the wire ABI");
#pragma pack(pop)

// Read/write helpers that respect the host's endianness. We standardize on
// little-endian on the wire, which matches every platform xrt-sync targets
// (ARM and x86-64 in their normal configurations). On the rare big-endian
// host we would byteswap here; for now we static_assert.
//
// MSVC does not define the GCC/Clang __BYTE_ORDER__ / __ORDER_LITTLE_ENDIAN__
// builtins. All Windows targets supported by MSVC (x86, x64, ARM, ARM64) are
// little-endian by the platform ABI, so we simply skip the assertion there.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "xrt-sync currently assumes a little-endian host");
#elif defined(_MSC_VER)
// MSVC: Windows ABI guarantees little-endian on all supported architectures.
#else
#error "xrt-sync requires a little-endian host; please port endianness handling for this toolchain"
#endif

inline void WriteHeader(void* dst, const Header& h) noexcept {
  std::memcpy(dst, &h, sizeof(Header));
}

inline bool ReadHeader(const void* src, std::size_t available,
                       Header* out) noexcept {
  if (available < sizeof(Header)) return false;
  std::memcpy(out, src, sizeof(Header));
  return out->magic == kMagic && out->protocol == kProtocolVersion;
}

}  // namespace xrtsync::wire

#endif  // XRTSYNC_SRC_WIRE_FORMAT_H_
