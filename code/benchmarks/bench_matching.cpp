#include <benchmark/benchmark.h>

#include "lob/FeedHandler.hpp"
#include "lob/Latency.hpp"
#include "lob/MatchingEngine.hpp"
#include "lob/MemoryPool.hpp"
#include "lob/OrderBook.hpp"
#include "lob/RingBuffer.hpp"

#include <chrono>
#include <string>
#include <vector>

using namespace lob;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Order make_order(uint64_t  id,
                        Side      side,
                        double    price,
                        uint64_t  qty,
                        OrderType type = OrderType::LIMIT) {
    return Order{id, side, type, price, qty, "AAPL",
                 std::chrono::nanoseconds{0}};
}

// ---------------------------------------------------------------------------
// OrderBook micro-benchmarks
// ---------------------------------------------------------------------------

static void BM_OrderBookAddBid(benchmark::State& state) {
    for (auto _ : state) {
        OrderBook book{"AAPL"};
        for (uint64_t i = 1; i <= static_cast<uint64_t>(state.range(0)); ++i)
            benchmark::DoNotOptimize(
                book.add_order(make_order(
                    i, Side::BUY,
                    100.0 - static_cast<double>(i % 10), 100)));
    }
}
BENCHMARK(BM_OrderBookAddBid)->Range(64, 8192)->Unit(benchmark::kMicrosecond);

static void BM_OrderBookAddAsk(benchmark::State& state) {
    for (auto _ : state) {
        OrderBook book{"AAPL"};
        for (uint64_t i = 1; i <= static_cast<uint64_t>(state.range(0)); ++i)
            benchmark::DoNotOptimize(
                book.add_order(make_order(
                    i, Side::SELL,
                    100.0 + static_cast<double>(i % 10), 100)));
    }
}
BENCHMARK(BM_OrderBookAddAsk)->Range(64, 8192)->Unit(benchmark::kMicrosecond);

static void BM_OrderBookCancel(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book{"AAPL"};
        for (uint64_t i = 1; i <= static_cast<uint64_t>(N); ++i)
            book.add_order(make_order(
                i, Side::BUY,
                100.0 - static_cast<double>(i % 10), 100));
        state.ResumeTiming();

        for (uint64_t i = 1; i <= static_cast<uint64_t>(N); ++i)
            benchmark::DoNotOptimize(book.cancel_order(i));
    }
}
BENCHMARK(BM_OrderBookCancel)->Range(64, 4096)->Unit(benchmark::kMicrosecond);

static void BM_OrderBookModify(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book{"AAPL"};
        for (uint64_t i = 1; i <= static_cast<uint64_t>(N); ++i)
            book.add_order(make_order(i, Side::BUY, 99.0, 100));
        state.ResumeTiming();

        for (uint64_t i = 1; i <= static_cast<uint64_t>(N); ++i)
            benchmark::DoNotOptimize(book.modify_order(i, 150));
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_OrderBookModify)->Range(64, 4096)->Unit(benchmark::kMicrosecond);

static void BM_OrderBookSnapshot(benchmark::State& state) {
    OrderBook book{"AAPL"};
    for (uint64_t i = 1; i <= 100; ++i) {
        book.add_order(make_order(i,       Side::BUY,
                                  100.0 - static_cast<double>(i % 10), 100));
        book.add_order(make_order(100 + i, Side::SELL,
                                  101.0 + static_cast<double>(i % 10), 100));
    }

    for (auto _ : state)
        benchmark::DoNotOptimize(
            book.snapshot(static_cast<std::size_t>(state.range(0))));
}
BENCHMARK(BM_OrderBookSnapshot)->Range(1, 20)->Unit(benchmark::kNanosecond);

static void BM_OrderBookImbalance(benchmark::State& state) {
    OrderBook book{"AAPL"};
    for (uint64_t i = 1; i <= 200; ++i) {
        book.add_order(make_order(i,       Side::BUY,
                                  100.0 - static_cast<double>(i % 10), 100));
        book.add_order(make_order(200 + i, Side::SELL,
                                  101.0 + static_cast<double>(i % 10), 100));
    }

    for (auto _ : state)
        benchmark::DoNotOptimize(book.imbalance());
}
BENCHMARK(BM_OrderBookImbalance)->Unit(benchmark::kNanosecond);

static void BM_OrderBookVWAP(benchmark::State& state) {
    OrderBook book{"AAPL"};
    for (uint64_t i = 1; i <= 100; ++i) {
        book.add_order(make_order(i,       Side::BUY,
                                  100.0 - static_cast<double>(i % 10), 100));
        book.add_order(make_order(100 + i, Side::SELL,
                                  101.0 + static_cast<double>(i % 10), 100));
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(book.bid_vwap(5));
        benchmark::DoNotOptimize(book.ask_vwap(5));
    }
}
BENCHMARK(BM_OrderBookVWAP)->Unit(benchmark::kNanosecond);

static void BM_OrderBookMarketImpact(benchmark::State& state) {
    OrderBook book{"AAPL"};
    for (uint64_t i = 1; i <= 50; ++i)
        book.add_order(make_order(i, Side::SELL,
                                  100.0 + static_cast<double>(i) * 0.01,
                                  1000));

    for (auto _ : state)
        benchmark::DoNotOptimize(
            book.estimate_market_impact(Side::BUY,
                static_cast<uint64_t>(state.range(0))));
}
BENCHMARK(BM_OrderBookMarketImpact)->Range(100, 10000)->Unit(benchmark::kNanosecond);

// ---------------------------------------------------------------------------
// MatchingEngine benchmarks
// ---------------------------------------------------------------------------

static void BM_MatchingEngineLimitResting(benchmark::State& state) {
    uint64_t id = 1;
    for (auto _ : state) {
        MatchingEngine engine;
        engine.register_symbol("AAPL");
        for (int i = 0; i < state.range(0); ++i) {
            auto o = make_order(id++, Side::BUY,
                                99.0 - static_cast<double>(i % 5), 100);
            benchmark::DoNotOptimize(engine.submit_order(std::move(o)));
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_MatchingEngineLimitResting)
    ->Range(64, 4096)
    ->Unit(benchmark::kMicrosecond);

static void BM_MatchingEngineFullMatch(benchmark::State& state) {
    uint64_t id = 1;
    for (auto _ : state) {
        MatchingEngine engine;
        engine.register_symbol("AAPL");
        engine.submit_order(make_order(id++, Side::SELL, 100.0, 10'000'000));
        for (int i = 0; i < state.range(0); ++i) {
            auto o = make_order(id++, Side::BUY, 100.0, 100, OrderType::MARKET);
            benchmark::DoNotOptimize(engine.submit_order(std::move(o)));
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_MatchingEngineFullMatch)
    ->Range(64, 4096)
    ->Unit(benchmark::kMicrosecond);

static void BM_MatchingEngineMultiLevelSweep(benchmark::State& state) {
    const int levels = static_cast<int>(state.range(0));
    uint64_t id = 1;
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        engine.register_symbol("AAPL");
        for (int i = 0; i < levels; ++i)
            engine.submit_order(make_order(
                id++, Side::SELL,
                100.0 + static_cast<double>(i) * 0.01, 100));
        state.ResumeTiming();

        auto o = make_order(id++, Side::BUY,
                            100.0 + static_cast<double>(levels) * 0.01,
                            static_cast<uint64_t>(100 * levels));
        benchmark::DoNotOptimize(engine.submit_order(std::move(o)));
    }
    state.SetItemsProcessed(state.iterations() * levels);
}
BENCHMARK(BM_MatchingEngineMultiLevelSweep)
    ->Range(1, 32)
    ->Unit(benchmark::kNanosecond);

static void BM_CancelViaEngine(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t id = 1;
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        engine.register_symbol("AAPL");
        std::vector<uint64_t> ids;
        ids.reserve(N);
        for (int i = 0; i < N; ++i) {
            auto oid = id++;
            engine.submit_order(make_order(oid, Side::BUY, 99.0, 100));
            ids.push_back(oid);
        }
        state.ResumeTiming();

        for (auto oid : ids)
            benchmark::DoNotOptimize(engine.cancel_order("AAPL", oid));
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_CancelViaEngine)->Range(64, 2048)->Unit(benchmark::kMicrosecond);

static void BM_IOCOrder(benchmark::State& state) {
    uint64_t id = 1;
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        engine.register_symbol("AAPL");
        // Set up N resting ask levels
        for (int i = 0; i < state.range(0); ++i)
            engine.submit_order(make_order(
                id++, Side::SELL,
                100.0 + static_cast<double>(i) * 0.01, 50));
        state.ResumeTiming();

        // IOC sweeps all levels then cancels remainder
        auto o = make_order(id++, Side::BUY, 200.0,
                            static_cast<uint64_t>(50 * state.range(0) + 1),
                            OrderType::IOC);
        benchmark::DoNotOptimize(engine.submit_order(std::move(o)));
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_IOCOrder)->Range(1, 32)->Unit(benchmark::kNanosecond);

static void BM_FOKAccepted(benchmark::State& state) {
    uint64_t id = 1;
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        engine.register_symbol("AAPL");
        engine.submit_order(make_order(id++, Side::SELL, 100.0, 10'000'000));
        state.ResumeTiming();

        auto o = make_order(id++, Side::BUY, 100.0,
                            static_cast<uint64_t>(state.range(0)),
                            OrderType::FOK);
        benchmark::DoNotOptimize(engine.submit_order(std::move(o)));
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_FOKAccepted)->Range(100, 100000)->Unit(benchmark::kNanosecond);

// ---------------------------------------------------------------------------
// RingBuffer benchmark
// ---------------------------------------------------------------------------

static void BM_RingBufferPushPop(benchmark::State& state) {
    RingBuffer<int, 65536> rb;
    for (auto _ : state) {
        for (int i = 0; i < state.range(0); ++i)
            benchmark::DoNotOptimize(rb.push(i));
        for (int i = 0; i < state.range(0); ++i)
            benchmark::DoNotOptimize(rb.pop());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_RingBufferPushPop)->Range(64, 8192)->Unit(benchmark::kNanosecond);

// ---------------------------------------------------------------------------
// MemoryPool benchmark
// ---------------------------------------------------------------------------

static void BM_MemoryPoolAllocDealloc(benchmark::State& state) {
    MemoryPool<Order, 4096> pool;
    std::vector<Order*> ptrs;
    ptrs.reserve(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        state.PauseTiming();
        ptrs.clear();
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            Order* p = pool.allocate();
            benchmark::DoNotOptimize(p);
            ptrs.push_back(p);
        }
        for (auto* p : ptrs)
            pool.deallocate(p);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_MemoryPoolAllocDealloc)->Range(64, 2048)->Unit(benchmark::kNanosecond);

// ---------------------------------------------------------------------------
// LatencyRecorder benchmark
// ---------------------------------------------------------------------------

static void BM_LatencyRecorderRecord(benchmark::State& state) {
    LatencyRecorder rec{static_cast<std::size_t>(state.range(0)) + 1};
    for (auto _ : state) {
        rec.clear();
        for (int i = 0; i < state.range(0); ++i)
            rec.record(std::chrono::nanoseconds{static_cast<int64_t>(i)});
        benchmark::DoNotOptimize(rec.count());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_LatencyRecorderRecord)->Range(100, 100000)->Unit(benchmark::kMicrosecond);

static void BM_LatencyRecorderPercentiles(benchmark::State& state) {
    LatencyRecorder rec;
    for (int i = 1; i <= state.range(0); ++i)
        rec.record(std::chrono::nanoseconds{static_cast<int64_t>(i)});

    for (auto _ : state) {
        benchmark::DoNotOptimize(rec.p50_ns());
        benchmark::DoNotOptimize(rec.p90_ns());
        benchmark::DoNotOptimize(rec.p99_ns());
        benchmark::DoNotOptimize(rec.p999_ns());
    }
}
BENCHMARK(BM_LatencyRecorderPercentiles)->Range(100, 100000)->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// Synthetic feed end-to-end benchmark
// ---------------------------------------------------------------------------

static void BM_SyntheticFeed(benchmark::State& state) {
    for (auto _ : state) {
        MatchingEngine engine;
        FeedHandler    feed{engine};

        FeedConfig cfg;
        cfg.type   = FeedType::SYNTHETIC;
        cfg.symbol = "AAPL";
        feed.configure(cfg);

        SyntheticConfig syn;
        syn.num_events   = static_cast<uint64_t>(state.range(0));
        syn.arrival_rate = 100'000.0;
        syn.mid_price    = 150.0;
        syn.tick_size    = 0.01;
        syn.seed         = 42;
        feed.generate_synthetic(syn);

        benchmark::DoNotOptimize(engine.stats());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_SyntheticFeed)
    ->Range(1000, 100000)
    ->Unit(benchmark::kMillisecond);

static void BM_SyntheticFeedHighCancelRate(benchmark::State& state) {
    for (auto _ : state) {
        MatchingEngine engine;
        FeedHandler    feed{engine};

        FeedConfig cfg;
        cfg.type   = FeedType::SYNTHETIC;
        cfg.symbol = "AAPL";
        feed.configure(cfg);

        SyntheticConfig syn;
        syn.num_events   = static_cast<uint64_t>(state.range(0));
        syn.arrival_rate = 100'000.0;
        syn.cancel_rate  = 0.7;
        syn.mid_price    = 150.0;
        syn.tick_size    = 0.01;
        syn.seed         = 7;
        feed.generate_synthetic(syn);

        benchmark::DoNotOptimize(engine.stats());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_SyntheticFeedHighCancelRate)
    ->Range(1000, 50000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
