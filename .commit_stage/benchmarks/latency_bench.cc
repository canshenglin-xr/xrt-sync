// Copyright (c) 2026 Cansheng LIN
// SPDX-License-Identifier: Apache-2.0
//
// latency_bench — measure one-way latency and jitter of an xrt-sync session.

#include <xrtsync/xrtsync.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Args {
    std::string mode = "send";
    std::string host = "127.0.0.1";
    uint16_t    port = 7900;
    int         hz = 120;
    int         payload = 64;
    int         duration_s = 30;
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq);
        std::string v = s.substr(eq + 1);
        if      (k == "--mode")     a.mode = v;
        else if (k == "--host")     a.host = v;
        else if (k == "--port")     a.port = static_cast<uint16_t>(std::stoi(v));
        else if (k == "--hz")       a.hz = std::stoi(v);
        else if (k == "--payload")  a.payload = std::stoi(v);
        else if (k == "--duration") a.duration_s = std::stoi(v);
    }
    return a;
}

uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void run_sender(const Args& a) {
    xrtsync::SessionConfig cfg;
    cfg.local_endpoint  = {"0.0.0.0", 0};
    cfg.remote_endpoint = {a.host, a.port};
    cfg.target_latency_ms = 25;
    xrtsync::Session session(cfg);
    session.start();

    std::vector<uint8_t> buf(a.payload, 0);
    const auto period = std::chrono::nanoseconds(1'000'000'000LL / a.hz);
    auto next = std::chrono::steady_clock::now();
    const auto end = next + std::chrono::seconds(a.duration_s);

    uint64_t seq = 0;
    while (std::chrono::steady_clock::now() < end) {
        uint64_t t = now_ns();
        std::memcpy(buf.data(), &t,   sizeof(t));
        std::memcpy(buf.data() + 8, &seq, sizeof(seq));
        xrtsync::StatePacket pkt;
        pkt.timestamp_ns = t;
        pkt.payload.assign(buf.begin(), buf.end());
        session.send(pkt);
        ++seq;
        next += period;
        std::this_thread::sleep_until(next);
    }
    std::printf("sent %llu packets\n", static_cast<unsigned long long>(seq));
}

void run_receiver(const Args& a) {
    xrtsync::SessionConfig cfg;
    cfg.local_endpoint  = {"0.0.0.0", a.port};
    cfg.remote_endpoint = {a.host, 0};
    xrtsync::Session session(cfg);
    session.start();

    std::vector<int64_t> samples;
    samples.reserve(static_cast<size_t>(a.hz) * static_cast<size_t>(a.duration_s));
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(a.duration_s + 2);
    while (std::chrono::steady_clock::now() < end) {
        if (auto s = session.poll(); s.has_value()) {
            int64_t latency_ns = static_cast<int64_t>(now_ns() - s->timestamp_ns);
            samples.push_back(latency_ns);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    if (samples.empty()) { std::printf("no samples\n"); return; }
    std::sort(samples.begin(), samples.end());
    auto pct = [&](double p) { return samples[static_cast<size_t>(p * (samples.size() - 1))]; };
    std::printf("samples=%zu  p50=%.3f ms  p95=%.3f ms  p99=%.3f ms  max=%.3f ms\n",
                samples.size(),
                pct(0.50) / 1e6, pct(0.95) / 1e6, pct(0.99) / 1e6, pct(1.0) / 1e6);
}

}  // namespace

int main(int argc, char** argv) {
    Args a = parse(argc, argv);
    if (a.mode == "send") run_sender(a);
    else                  run_receiver(a);
    return 0;
}
