# Performance Guide

## Table of Contents

1. [Throughput Targets](#throughput-targets)
2. [Latency Budget](#latency-budget)
3. [Benchmarks Overview](#benchmarks-overview)
4. [Profiling Workflow](#profiling-workflow)
5. [Hot-Path Optimisation Notes](#hot-path-optimisation-notes)
6. [Memory Layout](#memory-layout)
7. [Scaling Considerations](#scaling-considerations)

---

## Throughput Targets

Measured on a single core at 3.8 GHz with -O3 -march=native.

| Workload                              | Orders per second      | Notes                             |
| ------------------------------------- | ---------------------- | --------------------------------- |
| LIMIT orders, no cross (resting only) | 3,500,000 -- 5,000,000 | O(log L) per order                |
| LIMIT orders, single-level cross      | 2,000,000 -- 3,500,000 | One passive order consumed        |
| MARKET orders sweeping 5 levels       | 800,000 -- 1,500,000   | 5 passive orders per sweep        |
| Mixed synthetic workload (40% cancel) | 1,200,000 -- 2,000,000 | cancel_order is also O(log L + Q) |
| Full MLPipeline update per tick       | 600,000 -- 1,200,000   | Feature extraction + 3 models     |

---

## Latency Budget

Per-order latency measured with `ScopedTimer` over 100,000-event batches.

| Percentile | Typical (ns)    | With ML pipeline (ns) |
| ---------- | --------------- | --------------------- |
| p50        | 280 -- 420      | 620 -- 900            |
| p90        | 380 -- 580      | 850 -- 1,200          |
| p99        | 500 -- 800      | 1,100 -- 1,800        |
| p99.9      | 800 -- 1,400    | 1,800 -- 3,200        |
| max        | 5,000 -- 20,000 | 8,000 -- 30,000       |

Max latency is dominated by OS jitter and cache misses on cold code paths.
For deterministic ultra-low latency, use `SCHED_FIFO` or `SCHED_RR`, pin the
thread to an isolated core, and pre-warm the caches with a dry run.

---

## Benchmarks Overview

Run the full suite:

```bash
./build/quantlob_bench --benchmark_repetitions=3 --benchmark_report_aggregates_only=true
```

### Key benchmark IDs and what they measure

| Benchmark name                          | Measures                                     |
| --------------------------------------- | -------------------------------------------- |
| BM_OrderBookAddBid / BM_OrderBookAddAsk | add_order cost vs. number of existing levels |
| BM_OrderBookCancel                      | cancel_order including list removal          |
| BM_OrderBookModify                      | modify_order (no list change)                |
| BM_OrderBookSnapshot                    | snapshot(K) iteration cost                   |
| BM_OrderBookImbalance                   | bid_depth + ask_depth loop cost              |
| BM_OrderBookVWAP                        | bid_vwap + ask_vwap over 5 levels            |
| BM_OrderBookMarketImpact                | estimate_market_impact worst case            |
| BM_MatchingEngineLimitResting           | submit_order with no cross                   |
| BM_MatchingEngineFullMatch              | Market orders against a single large passive |
| BM_MatchingEngineMultiLevelSweep        | Sweep N levels per order                     |
| BM_CancelViaEngine                      | cancel_order via engine round-trip           |
| BM_IOCOrder                             | IOC sweep + cancel remainder                 |
| BM_FOKAccepted                          | FOK liquidity check + full fill              |
| BM_RingBufferPushPop                    | SPSC enqueue + dequeue                       |
| BM_MemoryPoolAllocDealloc               | Pool allocate + deallocate cycle             |
| BM_LatencyRecorderRecord                | Record N samples                             |
| BM_LatencyRecorderPercentiles           | Compute all percentiles over N samples       |
| BM_SyntheticFeed                        | End-to-end synthetic generation              |
| BM_SyntheticFeedHighCancelRate          | Same with 70% cancel rate                    |

### Benchmark flag reference

| Flag                                 | Example        | Description                   |
| ------------------------------------ | -------------- | ----------------------------- |
| `--benchmark_filter`                 | `BM_OrderBook` | Run only matching benchmarks  |
| `--benchmark_repetitions`            | `5`            | Repeat each benchmark N times |
| `--benchmark_out`                    | `results.json` | Write results to file         |
| `--benchmark_out_format`             | `json`         | Output format: json or csv    |
| `--benchmark_report_aggregates_only` | `true`         | Suppress per-iteration rows   |
| `--benchmark_min_time`               | `2.0`          | Minimum seconds per benchmark |

---

## Profiling Workflow

### 1. Build with debug info in Release mode

```bash
cmake -B build_prof -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build_prof --parallel
```

### 2. Profile with perf (Linux)

```bash
perf record -g --call-graph dwarf -- ./build_prof/quantlob_bench \
    --benchmark_filter=BM_SyntheticFeed \
    --benchmark_min_time=5.0
perf report --stdio | head -60
```

### 3. Identify hot functions

| Typical hot function        | Optimisation lever                               |
| --------------------------- | ------------------------------------------------ |
| `OrderBook::apply_fill`     | Reduce map lookups; fuse with cross()            |
| `std::list::remove`         | Replace list with flat array for small levels    |
| `std::unordered_map` rehash | Pre-reserve with `orders_.reserve(N)`            |
| `std::map` rotations        | Unavoidable; mitigate by keeping level count low |

### 4. Flamegraph

```bash
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

---

## Hot-Path Optimisation Notes

| Technique                                   | Where applied                   | Benefit                                                |
| ------------------------------------------- | ------------------------------- | ------------------------------------------------------ |
| Separate cache lines for atomic head/tail   | RingBuffer                      | Eliminates false sharing between producer and consumer |
| alignas(64) on pool storage and free-stack  | MemoryPool                      | Prevents false sharing on MPMC pool                    |
| `try_emplace` instead of find+insert        | MatchingEngine::register_symbol | Single map lookup                                      |
| In-place level erasure during crossing      | MatchingEngine::cross           | No extra pass after the sweep                          |
| `[[nodiscard]]` on query methods            | Throughout                      | Compiler warns on ignored results                      |
| `noexcept` on destructors and query methods | Throughout                      | Allows better inlining decisions                       |
| Pre-allocated `active_ids` vector           | FeedHandler::generate_synthetic | No reallocations during generation                     |
| Swap-and-pop for cancel removal             | FeedHandler::generate_synthetic | O(1) vector removal                                    |

---

## Memory Layout

### Per-symbol memory usage

| Structure                 | Per-level cost | Per-order cost | Notes                                        |
| ------------------------- | -------------- | -------------- | -------------------------------------------- |
| `BidMap` / `AskMap` entry | ~100 bytes     | 0              | std::map node: value + two pointers + colour |
| `PriceLevel.order_ids`    | 24 bytes base  | 32 bytes       | std::list node per ID                        |
| `orders_` unordered_map   | 0              | ~200 bytes     | std::string symbol adds ~32 bytes on stack   |

For a book with 20 price levels and 500 resting orders: approximately 100 KB.

### Strategies to reduce memory

| Strategy                                                    | Effect                                            | Trade-off                                                 |
| ----------------------------------------------------------- | ------------------------------------------------- | --------------------------------------------------------- |
| Replace `std::list<uint64_t>` with `std::vector<uint64_t>`  | ~50% reduction in per-order list overhead         | O(N) removal for cancel; acceptable if levels are shallow |
| Replace `std::string symbol` with a symbol index (uint16_t) | ~32 bytes per order                               | Requires symbol registry lookup                           |
| Use `MemoryPool<Order, N>` for order storage                | Eliminates allocator overhead for hot-path orders | Fixed capacity                                            |

---

## Scaling Considerations

| Dimension          | Current limit                  | Path to scale                                                          |
| ------------------ | ------------------------------ | ---------------------------------------------------------------------- |
| Symbols            | Unlimited (heap-allocated map) | Pre-allocate books array for cache locality                            |
| Price levels       | Unlimited                      | Use a pool-backed sorted structure for O(1) amortised level allocation |
| Concurrent symbols | 1 thread per engine            | Shard symbols across engine instances and aggregate stats              |
| Event rate         | ~5M orders/sec single-core     | Pipeline: decode on one core, match on another via RingBuffer          |
| Latency floor      | ~200 ns                        | DPDK + kernel bypass + busy polling for sub-100 ns                     |
