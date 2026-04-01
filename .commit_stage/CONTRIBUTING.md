# Contributing to xrt-sync

Thank you for your interest in contributing. This document covers the workflow, coding standards, and review expectations.

## Reporting Issues

- Use the GitHub issue tracker. Include OS, compiler version, exact CMake command line, and a minimal reproducer.
- Security-sensitive issues: do not file a public issue. Open a private discussion or email the maintainer (see `README.md` → Author).

## Pull Requests

1. Fork, branch off `main`, and open a PR against `main`.
2. Each PR should be a single logical change. Squash before merge if you have noisy fixup commits.
3. Every PR must:
   - Pass all CI matrix jobs (Windows MSVC, macOS Clang, Linux GCC + Clang, iOS arm64, Android arm64-v8a).
   - Update or add unit tests under `tests/`.
   - Update documentation under `docs/` if you change public API or wire format.
4. Use Conventional Commits style for commit messages:
   - `feat:`, `fix:`, `perf:`, `refactor:`, `docs:`, `test:`, `build:`, `ci:`, `chore:`
   - Example: `feat(jitter): make placeholder recovery extrapolator pluggable`

## Coding Standards

- C++17. No exceptions in the I/O hot path. Avoid heap allocation in `Session::poll`.
- Run `clang-format -i` against your changes before committing (style is `.clang-format` in repo root).
- Public headers under `include/xrtsync/` must remain pure C++17 with no platform-specific includes leaking out.
- Internal headers under `src/` may use platform conditionals.

## Wire-Format Changes

Any change to the on-the-wire packet layout must:

1. Bump the protocol version in `wire_format.h`.
2. Add a section to `docs/PROTOCOL.md` describing the change.
3. Include compatibility tests under `tests/wire_format_test.cc`.
4. Be approved by the maintainer.

## License

By submitting a contribution, you certify that you have the right to license your work under the Apache License 2.0 and that you do so.
