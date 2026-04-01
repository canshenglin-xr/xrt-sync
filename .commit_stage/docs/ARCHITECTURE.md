# xrt-sync — Architecture Document / 架构文档

**Version**: 0.3.1
**Status**: Working draft (last revised May 2026)
**Author**: Cansheng LIN

---

## 1. Design Goals / 设计目标

xrt-sync is engineered around four non-negotiable goals derived from years of hands-on observation of failures in cross-device interactive systems:

1. **Bounded end-to-end latency**. The 99th-percentile single-hop latency over a typical 5 GHz Wi-Fi link, with 1 % packet loss, must remain below 25 ms for 64-byte payloads at 120 Hz. This is the empirically observed threshold above which users report motion-to-photon discomfort in VR.
2. **Graceful degradation under loss**. When a packet is lost, the receive pipeline must never stall the rendering thread; it must instead supply an extrapolated placeholder state and reconcile when the late packet arrives.
3. **Engine and runtime independence**. The library must be embeddable in any host that can call into a C ABI — Unity (via P/Invoke), Unreal (third-party module), native Qt/Vulkan applications, Python (via ctypes/cffi), and Rust (via bindgen).
4. **Footprint discipline**. Total compiled size with examples and tests excluded must fit within 200 KB on every supported platform; no external dependency beyond the platform's standard C++17 runtime and the system socket API.

---

## 2. Layered Decomposition / 分层结构

```
                ┌─────────────────────────────────────────┐
                │      Public API  (xrtsync.h)            │   Layer 4
                │      C ABI       (c_abi.h)              │
                ├─────────────────────────────────────────┤
                │      Session Manager  (session.cc)      │   Layer 3
                │      - Lifecycle, threading, callbacks  │
                ├─────────────────────────────────────────┤
                │      Jitter Buffer  (jitter_buffer.h)   │   Layer 2
                │      - Adaptive depth                   │
                │      - Placeholder recovery             │
                │      - Out-of-order resequencing        │
                ├─────────────────────────────────────────┤
                │      Wire Format    (wire_format.h)     │   Layer 1
                │      - Packed UDP header                │
                │      - Sequence / timestamp / type      │
                ├─────────────────────────────────────────┤
                │      Platform Sockets (platform_socket) │   Layer 0
                │      - Winsock2 / BSD / Darwin / NDK    │
                └─────────────────────────────────────────┘
```

Each layer depends only on layers strictly beneath it. The Session Manager owns one Jitter Buffer per direction, one Platform Socket, and a small dedicated I/O thread; the public API exposes a non-blocking poll model that integrates with any host render loop.

---

## 3. Wire Format / 协议格式

The on-the-wire packet structure is intentionally minimal — 16 bytes of header plus payload:

| Offset | Field | Size | Notes |
|---|---|---|---|
| 0 | magic | 2 B | `0x58 0x52` ("XR") |
| 2 | version | 1 B | currently `0x01` |
| 3 | flags | 1 B | bit 0 = keyframe, bit 1 = placeholder ack, bits 2-7 reserved |
| 4 | sequence | 4 B | little-endian, monotonically increasing per direction |
| 8 | timestamp_ns | 8 B | sender wall-clock in nanoseconds since session start |
| 16 | payload | variable | host-defined |

All fields are little-endian. The header is packed (no padding) and validated on every receive. See [`PROTOCOL.md`](PROTOCOL.md) for the full normative specification.

---

## 4. Adaptive Jitter Buffer / 自适应抖动缓冲

The jitter buffer is the heart of xrt-sync. Its responsibilities:

1. **Resequencing** — Packets are inserted into an ordered map keyed by sequence number; up to a configurable window of 128 sequences ahead of the current read cursor is buffered.
2. **Depth adaptation** — The buffer maintains an exponentially weighted moving estimate of inter-arrival jitter. When the estimate exceeds the current depth, depth is increased by one slot (additive increase). When it falls below 25 % of depth for sixty consecutive reads, depth is decreased by one slot (multiplicative decrease with floor of 1).
3. **Placeholder recovery** — When the read cursor reaches a sequence that has not yet arrived and the buffer wait-budget is exhausted, the buffer emits a **placeholder packet** extrapolated from the two most recent received packets (linear or, optionally, quaternion-SLERP if the host registers a custom extrapolator). The flag bit `placeholder` is set so the host can choose to ignore or accept it.
4. **Reconciliation** — When the late packet eventually arrives, it is dropped if its sequence is already past the read cursor; otherwise it is delivered out-of-band so the host can correct drift if desired.

This design draws from RTP's adaptive playout literature (Ramjee et al., 1994) but adapts it for the much tighter latency budget of XR rather than audio/voice telephony.

---

## 5. Threading Model / 线程模型

- One **I/O thread** owns the socket and drains incoming packets into the receive jitter buffer.
- The **host thread** (typically the render or game-logic thread) calls `Session::poll()` non-blockingly.
- Outbound `Session::send()` enqueues into a lock-free SPSC ring; the I/O thread drains and transmits.
- No mutexes are held across system calls; the only synchronization primitives are two SPSC rings and one atomic flag for shutdown.

This avoids the classic priority-inversion problem where a render thread blocks on a mutex held by a slow network operation.

---

## 6. Engine-Agnostic Embedding / 引擎无关嵌入

The C ABI in `include/xrtsync/c_abi.h` exposes:

```c
xrtsync_session_t* xrtsync_session_create(const xrtsync_config_t* cfg);
int                xrtsync_session_start (xrtsync_session_t* s);
int                xrtsync_session_send  (xrtsync_session_t* s, const void* data, size_t len);
int                xrtsync_session_poll  (xrtsync_session_t* s, void* out_buf, size_t* in_out_len);
void               xrtsync_session_close (xrtsync_session_t* s);
```

This surface is small enough that Unity (P/Invoke), Unreal (third-party module), Python (ctypes), Rust (bindgen) and any other FFI-capable runtime can integrate within an afternoon. See [`INTEGRATION.md`](INTEGRATION.md) for worked examples.

---

## 7. Why Not Use Existing Solutions / 为何不直接使用现有方案

| Candidate | Why insufficient |
|---|---|
| **Unity Netcode for GameObjects** | C# only, requires entire Unity runtime, > 50 MB footprint, cannot embed in native medical-imaging or simulation software. |
| **Unreal Replication** | Locked to Unreal Engine and its actor model; not usable from Qt/Vulkan native apps. |
| **gRPC / Protobuf** | TCP based, no jitter buffer, no placeholder recovery; designed for RPC, not for sub-30 ms state synchronization. |
| **RakNet / SLikeNet** | Unmaintained since 2017, no iOS/Android first-class support, no engine-agnostic ABI. |
| **WebRTC data channels** | Excellent for browser clients but heavy on native — pulling in libwebrtc adds > 30 MB; xrt-sync targets a < 200 KB footprint. |
| **DDS (RTI / OpenDDS)** | Excellent for industrial telemetry but optimized for many-publisher discovery scenarios, not point-to-point low-latency XR. |

The conclusion: no existing open-source middleware fills the specific niche of **engine-agnostic, sub-30 ms, embeddable, mobile-capable, Apache-2.0-licensed state synchronization**. xrt-sync occupies that niche.

---

## 8. Future Work / 后续工作

- DTLS-1.3 transport for public-internet deployments (v0.4)
- Deterministic replay log for regression testing and forensic debugging (v0.4)
- WebRTC data-channel bridge for browser clients (v0.5)
- Multi-peer mesh topology with CRDT-backed state convergence (v0.6)
- Formal verification of the wire-format parser using TLA+ (v1.0)

---

## 9. References / 参考文献

- Ramjee, R., Kurose, J., Towsley, D., Schulzrinne, H. (1994). *Adaptive playout mechanisms for packetized audio applications in wide-area networks*. INFOCOM '94.
- IETF RFC 3550 — *RTP: A Transport Protocol for Real-Time Applications*.
- OpenXR 1.0 Specification — Khronos Group.
- Khronos Group glTF 2.0 — for payload schema compatibility considerations.
