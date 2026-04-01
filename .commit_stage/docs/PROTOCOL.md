# xrt-sync Wire Protocol Specification

**Version**: 1 (XRT/1)
**Status**: Stable for v0.3.x; subject to additive evolution in v0.4
**Author**: Cansheng LIN

---

## 1. Transport

xrt-sync v0.3.x runs exclusively over **UDP/IPv4 and UDP/IPv6**. Each session is a bidirectional pair of unicast endpoints; multicast is not supported in v0.3.x.

The middleware MUST NOT assume any specific MTU. The wire format is designed such that the typical payload (state vectors of 32-256 bytes) fits well within both IPv4 (576 B guaranteed) and IPv6 (1280 B guaranteed) minimum MTUs, so fragmentation is avoided.

## 2. Packet Layout

All multi-byte integer fields are little-endian.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Magic     |    Version    |     Flags     |   Reserved    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Sequence Number                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Timestamp (high 32 bits)                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Timestamp (low 32 bits)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                       Payload (variable)                      |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Octets | Description |
|---|---|---|
| Magic | 2 | Constant `0x58 0x52` ("XR" in ASCII). Receivers MUST drop packets with mismatched magic. |
| Version | 1 | Protocol version. This document specifies version `0x01`. |
| Flags | 1 | Bitfield (see §3). |
| Reserved | 1 | MUST be zero; receivers MUST ignore. Reserved for future use. |
| Sequence Number | 4 | Monotonically increasing per direction. Wraps modulo 2^32. |
| Timestamp | 8 | Sender's session-relative time in nanoseconds. |
| Payload | variable | Host-defined opaque bytes. |

Total header size is exactly **16 octets**, packed without padding.

## 3. Flags

| Bit | Name | Meaning |
|---|---|---|
| 0 | KEYFRAME | This packet contains a full state snapshot rather than a delta. |
| 1 | PLACEHOLDER_ACK | This packet acknowledges receipt of a placeholder reconciliation. |
| 2 | END_OF_SESSION | Sender is closing the session gracefully. |
| 3-7 | RESERVED | MUST be zero. |

## 4. Sequence and Timestamp Semantics

- The first packet sent in a given direction MUST carry sequence `0` and a timestamp of `0`.
- Timestamps are strictly monotonically non-decreasing within a single session direction.
- The receiver's jitter buffer uses the timestamp (not the local arrival time) as the primary ordering key; the sequence number is used as a tiebreaker and for loss detection.

## 5. Loss Detection and Placeholder Recovery

When the receiver's read cursor reaches sequence `N` and the packet has not arrived within the current adaptive wait budget, the receiver synthesizes a placeholder packet:

- Sequence: `N` (synthetic)
- Timestamp: linearly extrapolated from sequences `N-2` and `N-1`
- Payload: host-extrapolated (default: copy of `N-1`; host may register a custom extrapolator, e.g., quaternion SLERP for pose data)
- Flags: `PLACEHOLDER` bit set (bit 1)

If the late packet eventually arrives and its sequence is still within the receiver's reconciliation window (default: 8 sequences), the receiver delivers it out-of-band so the host can update its model.

## 6. Endpoint Identification

xrt-sync v0.3.x does not include a session-identifier field in the packet header. Instead, the (source-IP, source-port, dest-IP, dest-port) 4-tuple uniquely identifies a session. Implementations that need multiplexing should run multiple sockets.

A session-ID field is planned for v0.4 to support NAT traversal and multi-tenant servers.

## 7. Security

v0.3.x specifies **no transport-level security**. Operators MUST either:

- Deploy on a trusted LAN, or
- Wrap xrt-sync traffic in a VPN / WireGuard tunnel, or
- Wait for v0.4, which adds DTLS-1.3.

This design choice keeps the v0.3.x footprint minimal and the security boundary explicit.

## 8. Wire Compatibility Promise

Within the `XRT/1` major version, all future minor versions will be backward wire-compatible: any v0.3.0 receiver MUST accept packets from any future v0.x.y sender (and vice versa), provided no reserved flag bits are interpreted incorrectly.

## 9. Reference Implementation

The canonical implementation is the source tree rooted at `src/wire_format.h` and `src/session.cc`. The unit test suite at `tests/wire_format_test.cc` exercises every documented field and every error path.
