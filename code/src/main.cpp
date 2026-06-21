#include "lob/Exporter.hpp"
#include "lob/FeedHandler.hpp"
#include "lob/Latency.hpp"
#include "lob/Logger.hpp"
#include "lob/MatchingEngine.hpp"
#include "lob/OrderBook.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <csignal>
#include <string>

using namespace std;

static atomic<bool> g_shutdown{false};
static void handle_signal(int) noexcept {
    g_shutdown.store(true, memory_order_relaxed);
}

static void print_usage(const char* prog) {
    cout
        << "Usage: " << prog << " [OPTIONS]\n\n"
        << "Options:\n"
        << "  --symbol SYM         Instrument symbol (default: AAPL)\n"
        << "  --lobster MSG.csv    Replay LOBSTER message CSV\n"
        << "  --realtime           Enable real-time replay pacing\n"
        << "  --speed FACTOR       Replay speed multiplier (default: 1.0)\n"
        << "  --events N           Synthetic event count (default: 500000)\n"
        << "  --mid PRICE          Synthetic initial mid price (default: 150.0)\n"
        << "  --tick SIZE          Tick size (default: 0.01)\n"
        << "  --seed N             RNG seed (default: 12345)\n"
        << "  --levels N           Snapshot depth (default: 5)\n"
        << "  --out-dir DIR        Output directory for CSV/text exports\n"
        << "  --export-trades      Export trade log to CSV\n"
        << "  --export-snapshot    Export final order book snapshot\n"
        << "  --export-latency     Export per-order latency samples\n"
        << "  --export-timeseries  Export periodic LOB snapshots time-series\n"
        << "  --snap-interval N    Events between time-series snapshots (default: 1000)\n"
        << "  --log-level LEVEL    DEBUG|INFO|WARN|ERROR (default: INFO)\n"
        << "  --help               Show this help\n\n"
        << "Examples:\n"
        << "  " << prog << "\n"
        << "  " << prog << " --symbol TSLA --events 1000000 --out-dir /tmp/lob\n"
        << "  " << prog << " --lobster code/data/sample/messages.csv"
                           " --export-snapshot --out-dir out\n"
        << "  " << prog << " --events 200000 --export-latency"
                           " --export-trades --out-dir out\n";
}

static string get_arg(int argc, char* argv[], const string& flag,
                            const string& def = "") {
    for (int i = 1; i < argc - 1; ++i)
        if (string(argv[i]) == flag) return argv[i + 1];
    return def;
}

static bool has_flag(int argc, char* argv[], const string& flag) {
    for (int i = 1; i < argc; ++i)
        if (string(argv[i]) == flag) return true;
    return false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    if (has_flag(argc, argv, "--help")) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    // -----------------------------------------------------------------------
    // Parse CLI arguments
    // -----------------------------------------------------------------------
    const string symbol       = get_arg(argc, argv, "--symbol",  "AAPL");
    const string lobster_msg  = get_arg(argc, argv, "--lobster", "");
    const bool        realtime     = has_flag(argc, argv, "--realtime");
    const double      speed        = stod(get_arg(argc, argv, "--speed",   "1.0"));
    const uint64_t    num_events   = stoull(get_arg(argc, argv, "--events","500000"));
    const double      mid_price    = stod(get_arg(argc, argv, "--mid",    "150.0"));
    const double      tick_size    = stod(get_arg(argc, argv, "--tick",   "0.01"));
    const uint32_t    seed         = static_cast<uint32_t>(
                                         stoul(get_arg(argc, argv, "--seed","12345")));
    const size_t levels       = static_cast<size_t>(
                                         stoul(get_arg(argc, argv, "--levels","5")));
    const string out_dir      = get_arg(argc, argv, "--out-dir", "");
    const bool        exp_trades   = has_flag(argc, argv, "--export-trades");
    const bool        exp_snap     = has_flag(argc, argv, "--export-snapshot");
    const bool        exp_lat      = has_flag(argc, argv, "--export-latency");
    const bool        exp_ts       = has_flag(argc, argv, "--export-timeseries");
    const uint64_t    snap_interval= stoull(
                                         get_arg(argc, argv, "--snap-interval","1000"));

    // -----------------------------------------------------------------------
    // Logger
    // -----------------------------------------------------------------------
    {
        string ls = get_arg(argc, argv, "--log-level", "INFO");
        lob::LogLevel lvl = lob::LogLevel::INFO;
        if      (ls == "DEBUG") lvl = lob::LogLevel::DEBUG;
        else if (ls == "WARN")  lvl = lob::LogLevel::WARN;
        else if (ls == "ERROR") lvl = lob::LogLevel::ERROR;
        lob::Logger::instance().set_level(lvl);
    }

    // -----------------------------------------------------------------------
    // Output directory
    // -----------------------------------------------------------------------
    filesystem::path out_path;
    if (!out_dir.empty()) {
        out_path = out_dir;
        filesystem::create_directories(out_path);
    }

    // -----------------------------------------------------------------------
    // Engine + latency recorder
    // -----------------------------------------------------------------------
    lob::MatchingEngine  engine;
    lob::LatencyRecorder recorder{static_cast<size_t>(num_events + 1024)};

    // -----------------------------------------------------------------------
    // Trade log
    // -----------------------------------------------------------------------
    ofstream trade_log;
    if (exp_trades && !out_path.empty()) {
        trade_log.open(out_path / "trades.csv");
        lob::Exporter::write_trade_header(trade_log);
    }

    engine.set_trade_callback([&](const lob::Trade& t) {
        if (trade_log.is_open())
            lob::Exporter::write_trade(trade_log, t);
    });

    engine.set_reject_callback([](uint64_t id, const string& reason) {
        LOB_WARN("Engine",
                 "Order " + to_string(id) + " rejected: " + reason);
    });

    // -----------------------------------------------------------------------
    // Time-series snapshot stream
    // -----------------------------------------------------------------------
    ofstream ts_log;
    uint64_t      ts_event_counter = 0;

    if (exp_ts && !out_path.empty()) {
        ts_log.open(out_path / "lob_timeseries.csv");
        lob::Exporter::write_timeseries_header(ts_log);
    }

    // -----------------------------------------------------------------------
    // Feed handler setup
    // -----------------------------------------------------------------------
    lob::FeedConfig cfg;
    cfg.symbol          = symbol;
    cfg.replay_realtime = realtime;
    cfg.replay_speed    = speed;
    const bool use_lobster = !lobster_msg.empty();
    cfg.type = use_lobster ? lob::FeedType::LOBSTER_CSV : lob::FeedType::SYNTHETIC;
    if (use_lobster) cfg.message_file = lobster_msg;

    lob::FeedHandler feed{engine};
    feed.configure(cfg);

    // Order callback: used for per-order latency measurement and time-series
    // snapshot emission.
    feed.set_order_callback([&](const lob::Order& /*o*/) {
        // Time-series snapshots at regular event intervals.
        if (exp_ts && ts_log.is_open()) {
            if (++ts_event_counter % snap_interval == 0) {
                const lob::OrderBook* book = engine.get_book(symbol);
                if (book) {
                    auto snap = book->snapshot(levels);
                    lob::Exporter::append_snapshot_row(ts_log, snap);
                }
            }
        }
    });

    LOB_INFO("Main", "Starting QuantLOB — symbol: " + symbol);

    // -----------------------------------------------------------------------
    // Run
    // -----------------------------------------------------------------------
    auto t0 = chrono::high_resolution_clock::now();

    if (use_lobster) {
        try {
            auto events = feed.load_lobster_csv(lobster_msg);
            cout << "Loaded " << events.size() << " LOBSTER events\n";
            feed.replay_lobster(events, symbol);
        } catch (const exception& ex) {
            cerr << "Error: " << ex.what() << "\n";
            return EXIT_FAILURE;
        }
    } else {
        lob::SyntheticConfig syn;
        syn.num_events   = num_events;
        syn.mid_price    = mid_price;
        syn.tick_size    = tick_size;
        syn.arrival_rate = 100'000.0;
        syn.seed         = seed;

        if (exp_lat) {
            // Measure per-batch average latency by timing chunks of the run.
            constexpr uint64_t BATCH = 1000;
            uint64_t           remaining   = num_events;
            uint32_t           batch_seed  = seed;

            while (remaining > 0 && !g_shutdown.load()) {
                const uint64_t batch_sz = min(remaining, BATCH);
                lob::SyntheticConfig batch_cfg = syn;
                batch_cfg.num_events = batch_sz;
                batch_cfg.seed       = batch_seed++;

                auto t_start = chrono::high_resolution_clock::now();
                feed.generate_synthetic(batch_cfg);
                auto t_end   = chrono::high_resolution_clock::now();

                auto batch_ns = chrono::duration_cast<chrono::nanoseconds>(
                                    t_end - t_start).count();
                // Record average per-event latency for this batch.
                recorder.record(chrono::nanoseconds{
                    batch_ns / static_cast<int64_t>(batch_sz)});

                remaining -= batch_sz;
            }
        } else {
            feed.generate_synthetic(syn);
        }
    }

    auto t1 = chrono::high_resolution_clock::now();

    if (g_shutdown.load())
        LOB_WARN("Main", "Shutdown signal received — results may be partial");

    // -----------------------------------------------------------------------
    // Report
    // -----------------------------------------------------------------------
    const double   ms    = chrono::duration<double, milli>(t1 - t0).count();
    const auto&    stats = engine.stats();
    const auto*    book  = engine.get_book(symbol);

    cout << fixed << setprecision(2);
    cout << "\n=== QuantLOB Engine Report ===\n\n"
              << "Symbol              : " << symbol                        << "\n"
              << "Wall time           : " << ms                            << " ms\n"
              << "Events processed    : " << feed.events_processed()       << "\n"
              << "Orders processed    : " << stats.orders_processed        << "\n"
              << "Orders matched      : " << stats.orders_matched          << "\n"
              << "Orders resting      : " << stats.orders_resting          << "\n"
              << "Orders cancelled    : " << stats.orders_cancelled        << "\n"
              << "Orders rejected     : " << stats.orders_rejected         << "\n"
              << "Total trades        : " << stats.total_trades            << "\n"
              << "Total volume        : " << stats.total_volume            << "\n"
              << "Total notional      : " << stats.total_notional          << "\n";

    if (ms > 0.0)
        cout << "Throughput          : "
                  << static_cast<uint64_t>(
                         stats.orders_processed / (ms / 1000.0))
                  << " orders/sec\n";

    if (book) {
        const auto snap   = book->snapshot(levels);
        const auto bid    = book->best_bid();
        const auto ask    = book->best_ask();
        const auto mid    = book->mid_price();
        const auto spread = book->spread();
        const auto rsprd  = book->relative_spread();
        const auto imbal  = book->imbalance();

        cout << "\n--- Order Book Snapshot: " << symbol << " ---\n";
        if (bid)    cout << "Best bid       : " << *bid    << "\n";
        if (ask)    cout << "Best ask       : " << *ask    << "\n";
        if (mid)    cout << "Mid price      : " << *mid    << "\n";
        if (spread) cout << "Spread         : " << *spread << "\n";
        if (rsprd)  cout << "Rel. spread    : " << setprecision(4)
                              << *rsprd * 100.0 << " bps\n";
        cout << setprecision(4)
                  << "Imbalance      : " << imbal  << "\n"
                  << setprecision(2)
                  << "Resting orders : " << book->order_count()      << "\n"
                  << "Bid levels     : " << book->level_count_bids() << "\n"
                  << "Ask levels     : " << book->level_count_asks() << "\n";

        // VWAP on both sides
        auto bvwap = book->bid_vwap(levels);
        auto avwap = book->ask_vwap(levels);
        if (bvwap.valid)
            cout << "Bid VWAP       : " << bvwap.vwap
                      << "  (qty " << bvwap.total_qty << ")\n";
        if (avwap.valid)
            cout << "Ask VWAP       : " << avwap.vwap
                      << "  (qty " << avwap.total_qty << ")\n";

        cout << "\nTop " << levels << " Bids:\n"
                  << setw(16) << "Price" << setw(14) << "Quantity\n";
        for (auto& [p, q] : snap.bids)
            cout << setw(16) << p << setw(14) << q << "\n";

        cout << "\nTop " << levels << " Asks:\n"
                  << setw(16) << "Price" << setw(14) << "Quantity\n";
        for (auto& [p, q] : snap.asks)
            cout << setw(16) << p << setw(14) << q << "\n";

        // ---------------------------------------------------------------
        // Exports
        // ---------------------------------------------------------------
        if (!out_path.empty()) {
            if (exp_snap) {
                auto p = out_path / (symbol + "_snapshot.csv");
                lob::Exporter::export_snapshot(snap, p);
                cout << "\nSnapshot     -> " << p << "\n";
            }
            if (exp_lat && recorder.count() > 0) {
                auto p = out_path / "latency.csv";
                lob::Exporter::export_latency(recorder, p);
                auto ps = out_path / "latency_summary.txt";
                lob::Exporter::export_latency_summary(recorder, ps);
                cout << "Latency      -> " << p
                          << "  (" << recorder.count() << " samples)\n"
                          << "  mean=" << static_cast<uint64_t>(recorder.mean_ns()) << "ns"
                          << "  p50="  << recorder.p50_ns()  << "ns"
                          << "  p99="  << recorder.p99_ns()  << "ns"
                          << "  p99.9="<< recorder.p999_ns() << "ns\n";
            }
            {
                auto p = out_path / "stats.txt";
                lob::Exporter::export_stats(stats, p);
                cout << "Stats        -> " << p << "\n";
            }
            if (exp_ts && ts_log.is_open())
                cout << "Time-series  -> "
                          << (out_path / "lob_timeseries.csv") << "\n";
        }
    }

    cout << "\n";
    return EXIT_SUCCESS;
}
