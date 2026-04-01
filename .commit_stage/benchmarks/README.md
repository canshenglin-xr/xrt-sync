# Benchmarks

This directory contains microbenchmarks used to validate the latency and recovery
claims in the top-level `README.md`.

## latency_bench

Measures one-way latency and end-to-end jitter for a sender/receiver pair over loopback
or any reachable UDP endpoint.

```bash
cmake -S .. -B ../build -DXRTSYNC_BUILD_BENCHMARKS=ON
cmake --build ../build -j

# Terminal 1
./build/benchmarks/latency_bench --mode=recv --port=7900

# Terminal 2
./build/benchmarks/latency_bench --mode=send --host=127.0.0.1 --port=7900 \
    --hz=120 --payload=64 --duration=30
```

The sender prints a histogram and percentile summary every second; the receiver
prints reordering and loss statistics.

## jitter_recovery_bench

Injects synthetic packet loss (uniform, bursty, and reorder-heavy patterns) and
measures the effective recovery rate of the placeholder mechanism. Used to produce
the table in §`Performance` of the top-level README.

## Reproducibility

All benchmarks pin themselves to a single core (via `pthread_setaffinity_np` on
Linux, `SetThreadAffinityMask` on Windows) and disable CPU frequency scaling on
Linux when run with `sudo`. Results without core pinning will show significantly
wider tail latencies.
