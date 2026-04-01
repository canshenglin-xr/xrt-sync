# Changelog

All notable changes to xrt-sync are documented in this file. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned for 0.4.0
- DTLS-1.3 transport for public-internet deployments
- Deterministic replay log with hash-chained event records
- Session-ID field in wire header to support NAT traversal and multi-tenant servers

## [0.3.1] — 2026-05

### Fixed
- Race condition in `Session::stop()` where the I/O thread could observe a partially destroyed jitter buffer
- Off-by-one in placeholder extrapolation when only one prior packet had been received
- Windows: `WSAStartup` was not paired with `WSACleanup` on early-exit paths

### Documentation
- Expanded `docs/ARCHITECTURE.md` with explicit comparison table against Unity Netcode, Unreal Replication, gRPC, RakNet, WebRTC, DDS
- Added Python `ctypes` and Rust `bindgen` integration walkthroughs to `docs/INTEGRATION.md`

## [0.3.0] — 2026-05

### Added
- Adaptive jitter buffer with placeholder recovery (additive-increase / multiplicative-decrease depth control)
- C ABI surface (`include/xrtsync/c_abi.h`) to support embedding from Unity, Unreal, Python, and Rust
- iOS (arm64) and Android (arm64-v8a) cross-compile targets in CI matrix
- `examples/pose_streamer` demo at 120 Hz

### Changed
- Migrated wire format to packed 16-byte header (was 20 bytes in 0.2.x); bumped protocol version to `0x01`
- Public API renamed `Session::tick()` → `Session::poll()` for clarity (non-blocking semantics unchanged)

### Removed
- Legacy TCP transport (was never officially supported; removed to simplify the receive path)

## [0.2.0] — 2026-04

### Added
- Initial cross-platform socket abstraction covering Windows (Winsock2), macOS / Linux (BSD), Android NDK
- Sequence-number based out-of-order resequencing (fixed-depth buffer; adaptive depth came in 0.3.0)
- Unit test suite under `tests/`

### Known Issues
- Buffer depth is not yet adaptive; tuning is manual
- Loss recovery is reactive only (no placeholder synthesis)

## [0.1.0] — 2026-04

### Added
- Initial project skeleton: CMake build, public header, minimal UDP send/receive loop on Linux only
- Apache 2.0 license, README, contributing guide
