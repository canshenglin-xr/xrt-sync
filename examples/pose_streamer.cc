// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Cansheng LIN
//
// Minimal example: stream a synthetic 6-DOF pose at 90 Hz from a client
// to a host. Demonstrates the typical AR/XR send/receive loop and shows
// how an engine integration would wire xrt-sync into a frame callback.
//
// Build (after `cmake --build build`):
//
//   ./build/examples/pose_streamer host 127.0.0.1:5555
//   ./build/examples/pose_streamer client 127.0.0.1:5555
//
// The host prints inbound poses; the client emits a sine-wave trajectory.

#include "xrtsync/xrtsync.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

struct Pose6DOF {
  float position[3];
  float quaternion[4];
};

std::atomic<bool> g_should_exit{false};

void HandleSignal(int) { g_should_exit = true; }

int RunHost(const std::string& endpoint) {
  xrtsync::SessionConfig config;
  config.endpoint = endpoint;
  config.is_host = true;
  config.send_rate_hz = 240;

  auto [session, status] = xrtsync::Session::Create(config);
  if (status != xrtsync::Status::kOk) {
    std::fprintf(stderr, "host: failed to create session: %.*s\n",
                 static_cast<int>(xrtsync::StatusToString(status).size()),
                 xrtsync::StatusToString(status).data());
    return 1;
  }

  session->SetUpdateHandler(
      xrtsync::ChannelTag("pose"),
      [](const xrtsync::StateUpdate& u) noexcept {
        if (u.payload_size != sizeof(Pose6DOF)) return;
        Pose6DOF p{};
        std::memcpy(&p, u.payload, sizeof(p));
        std::printf("[host] seq=%u t=%lld pos=(%.3f %.3f %.3f) q=(%.3f %.3f %.3f %.3f)\n",
                    u.sequence, static_cast<long long>(u.timestamp),
                    p.position[0], p.position[1], p.position[2],
                    p.quaternion[0], p.quaternion[1],
                    p.quaternion[2], p.quaternion[3]);
      });

  std::printf("host listening on %s\n", endpoint.c_str());
  while (!g_should_exit.load()) {
    session->Tick(std::chrono::milliseconds(4));
  }
  return 0;
}

int RunClient(const std::string& host_endpoint) {
  xrtsync::SessionConfig config;
  config.endpoint = host_endpoint;
  config.is_host = false;
  config.send_rate_hz = 90;
  config.stale_threshold_us = 16'000;  // one frame at 60 Hz

  auto [session, status] = xrtsync::Session::Create(config);
  if (status != xrtsync::Status::kOk) {
    std::fprintf(stderr, "client: failed to create session\n");
    return 1;
  }

  std::printf("client streaming pose to %s at 90 Hz\n", host_endpoint.c_str());

  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  while (!g_should_exit.load()) {
    auto now = clock::now();
    double t = std::chrono::duration<double>(now - t0).count();

    Pose6DOF p{};
    p.position[0] = static_cast<float>(std::sin(t * 0.5));
    p.position[1] = 1.7f + static_cast<float>(std::sin(t * 0.25) * 0.05);
    p.position[2] = static_cast<float>(std::cos(t * 0.5));
    p.quaternion[0] = 0.f;
    p.quaternion[1] = static_cast<float>(std::sin(t * 0.5 * 0.5));
    p.quaternion[2] = 0.f;
    p.quaternion[3] = static_cast<float>(std::cos(t * 0.5 * 0.5));

    session->Send(xrtsync::ChannelTag("pose"), &p, sizeof(p));
    session->Tick(std::chrono::microseconds(500));
    std::this_thread::sleep_for(std::chrono::milliseconds(11));
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <host|client> <endpoint>\n", argv[0]);
    return 2;
  }
  std::string mode = argv[1];
  std::string endpoint = argv[2];
  if (mode == "host") return RunHost(endpoint);
  if (mode == "client") return RunClient(endpoint);
  std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
  return 2;
}
