// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN
//
// Unit tests for the JitterBuffer template. No external test framework is
// required — we use a tiny in-file harness so the test binary stays
// trivially buildable across every platform.

#include "../src/jitter_buffer.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

#define EXPECT(cond, msg)                                                   \
  do {                                                                      \
    if (cond) {                                                             \
      ++g_pass;                                                             \
    } else {                                                                \
      ++g_fail;                                                             \
      std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);  \
    }                                                                       \
  } while (0)

using Payload = std::vector<std::uint8_t>;

xrtsync::JitterBuffer<Payload> MakeBuffer() {
  xrtsync::JitterBuffer<Payload> buf;
  buf.Configure(std::chrono::microseconds(2000),
                std::chrono::microseconds(32000));
  return buf;
}

void TestInOrderInsertionAndPop() {
  auto buf = MakeBuffer();
  for (std::uint32_t i = 1; i <= 5; ++i) {
    buf.Insert(i, /*ts=*/i * 1000, Payload(8, static_cast<std::uint8_t>(i)));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  std::uint32_t popped = 0;
  while (auto e = buf.Pop()) {
    EXPECT(e->sequence == popped + 1, "sequences come out in order");
    ++popped;
  }
  EXPECT(popped == 5, "all five entries popped");
}

void TestOutOfOrderInsertion() {
  auto buf = MakeBuffer();
  buf.Insert(3, 3000, Payload(8, 3));
  buf.Insert(1, 1000, Payload(8, 1));
  buf.Insert(2, 2000, Payload(8, 2));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  auto a = buf.Pop();
  auto b = buf.Pop();
  auto c = buf.Pop();
  EXPECT(a && a->sequence == 1, "out-of-order: first is seq 1");
  EXPECT(b && b->sequence == 2, "out-of-order: second is seq 2");
  EXPECT(c && c->sequence == 3, "out-of-order: third is seq 3");
}

void TestDuplicateSilentlyDropped() {
  auto buf = MakeBuffer();
  buf.Insert(1, 1000, Payload(8, 1));
  buf.Insert(1, 1000, Payload(8, 1));
  EXPECT(buf.size() == 1, "duplicate insertion is ignored");
}

void TestLossEmitsPlaceholder() {
  auto buf = MakeBuffer();
  buf.Insert(1, 1000, Payload(8, 1));
  // Skip 2. Insert 3 and 4 — the gap should eventually be filled with a
  // placeholder after the wait budget elapses.
  buf.Insert(3, 3000, Payload(8, 3));
  buf.Insert(4, 4000, Payload(8, 4));
  std::this_thread::sleep_for(std::chrono::milliseconds(40));

  auto first = buf.Pop();
  EXPECT(first && first->sequence == 1 && !first->is_placeholder,
         "first popped is real seq 1");
  auto second = buf.Pop();
  EXPECT(second && second->sequence == 2 && second->is_placeholder,
         "second popped is placeholder for missing seq 2");
  auto third = buf.Pop();
  EXPECT(third && third->sequence == 3 && !third->is_placeholder,
         "third popped is real seq 3");
}

void TestLatePacketDropped() {
  auto buf = MakeBuffer();
  buf.Insert(1, 1000, Payload(8, 1));
  buf.Insert(2, 2000, Payload(8, 2));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  (void)buf.Pop();
  (void)buf.Pop();
  // Now try to insert an older packet — it must be dropped.
  buf.Insert(1, 1000, Payload(8, 1));
  EXPECT(buf.size() == 0, "late packet older than playout is dropped");
}

}  // namespace

int main() {
  TestInOrderInsertionAndPop();
  TestOutOfOrderInsertion();
  TestDuplicateSilentlyDropped();
  TestLossEmitsPlaceholder();
  TestLatePacketDropped();

  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
