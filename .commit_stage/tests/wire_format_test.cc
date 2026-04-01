// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN

#include "../src/wire_format.h"

#include <cstdio>
#include <cstring>

int main() {
  using namespace xrtsync::wire;
  std::uint8_t buf[64] = {0};
  Header h{};
  h.magic = kMagic;
  h.protocol = kProtocolVersion;
  h.type = static_cast<std::uint8_t>(PacketType::kStateUpdate);
  h.session_id = 0xDEADBEEF;
  h.participant_id = 42;
  h.sequence = 7;
  h.timestamp_us = 1234567890;
  WriteHeader(buf, h);

  Header parsed{};
  if (!ReadHeader(buf, sizeof(buf), &parsed)) {
    std::fprintf(stderr, "FAIL: ReadHeader rejected valid packet\n");
    return 1;
  }
  if (parsed.magic != kMagic || parsed.sequence != 7 ||
      parsed.session_id != 0xDEADBEEF || parsed.timestamp_us != 1234567890) {
    std::fprintf(stderr, "FAIL: round-trip mismatch\n");
    return 1;
  }

  // Truncated packet must be rejected.
  Header truncated{};
  if (ReadHeader(buf, sizeof(Header) - 1, &truncated)) {
    std::fprintf(stderr, "FAIL: truncated header accepted\n");
    return 1;
  }

  // Mismatched magic must be rejected.
  std::memset(buf, 0, sizeof(buf));
  Header bad{};
  if (ReadHeader(buf, sizeof(buf), &bad)) {
    std::fprintf(stderr, "FAIL: zero-magic header accepted\n");
    return 1;
  }

  std::printf("wire_format_test: ok\n");
  return 0;
}
