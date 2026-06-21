#pragma once

#include "Latency.hpp"
#include "MatchingEngine.hpp"
#include "OrderBook.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace lob {

/// Exports order book snapshots, latency samples, engine stats, and trade logs
/// to CSV/text files consumed by scripts/python/visualize_lob.py.
class Exporter {
public:
    // ------------------------------------------------------------------
    // Snapshot
    // ------------------------------------------------------------------

    /// Export a BookSnapshot to a CSV file.
    /// Format (header row + data rows):
    ///   side,price,quantity
    ///   BID,99.50,1000
    ///   ASK,100.50,500
    static void export_snapshot(const BookSnapshot&          snap,
                                const filesystem::path& path) {
        ofstream f = open_write(path);
        f << fixed << setprecision(6);
        f << "side,price,quantity\n";
        for (auto& [p, q] : snap.bids)
            f << "BID," << p << "," << q << "\n";
        for (auto& [p, q] : snap.asks)
            f << "ASK," << p << "," << q << "\n";
    }

    // ------------------------------------------------------------------
    // Latency
    // ------------------------------------------------------------------

    /// Export latency samples — one nanosecond value per line.
    /// Header: latency_ns
    static void export_latency(const LatencyRecorder&       rec,
                               const filesystem::path& path) {
        ofstream f = open_write(path);
        f << "latency_ns\n";
        for (size_t i = 0; i < rec.count(); ++i)
            f << rec.sample(i) << "\n";
    }

    /// Export a summary statistics file (key=value format).
    static void export_latency_summary(const LatencyRecorder&       rec,
                                       const filesystem::path& path) {
        ofstream f = open_write(path);
        f << fixed << setprecision(2);
        f << "count="    << rec.count()                                         << "\n"
          << "mean_ns="  << static_cast<uint64_t>(rec.mean_ns())                << "\n"
          << "stddev_ns="<< static_cast<uint64_t>(rec.stddev_ns())              << "\n"
          << "min_ns="   << rec.min_ns()                                        << "\n"
          << "p50_ns="   << rec.p50_ns()                                        << "\n"
          << "p90_ns="   << rec.p90_ns()                                        << "\n"
          << "p99_ns="   << rec.p99_ns()                                        << "\n"
          << "p999_ns="  << rec.p999_ns()                                       << "\n"
          << "max_ns="   << rec.max_ns()                                        << "\n";
    }

    // ------------------------------------------------------------------
    // Engine stats
    // ------------------------------------------------------------------

    /// Export engine stats to a simple key=value file.
    static void export_stats(const EngineStats&           stats,
                             const filesystem::path& path) {
        ofstream f = open_write(path);
        f << fixed << setprecision(2);
        f << "orders_processed="  << stats.orders_processed  << "\n"
          << "orders_matched="    << stats.orders_matched    << "\n"
          << "orders_resting="    << stats.orders_resting    << "\n"
          << "orders_cancelled="  << stats.orders_cancelled  << "\n"
          << "orders_rejected="   << stats.orders_rejected   << "\n"
          << "total_trades="      << stats.total_trades      << "\n"
          << "total_volume="      << stats.total_volume      << "\n"
          << "total_notional="    << stats.total_notional    << "\n";
    }

    // ------------------------------------------------------------------
    // Trade log  (streaming writes — keep file open across many calls)
    // ------------------------------------------------------------------

    /// Write the trade log CSV header to an already-open stream.
    static void write_trade_header(ofstream& f) {
        f << "timestamp_ns,buy_order_id,sell_order_id,price,quantity,notional\n";
    }

    /// Append a single trade to an already-open trade log stream.
    static void write_trade(ofstream& f, const Trade& t) {
        f << t.timestamp.count()    << ","
          << t.buy_order_id         << ","
          << t.sell_order_id        << ","
          << fixed << setprecision(6) << t.price << ","
          << t.quantity             << ","
          << fixed << setprecision(2)
          << (t.price * static_cast<double>(t.quantity)) << "\n";
    }

    // ------------------------------------------------------------------
    // Multi-snapshot time-series
    // ------------------------------------------------------------------

    /// Append a snapshot row to a time-series CSV.
    /// Schema: timestamp_ns,side,price,quantity
    static void append_snapshot_row(ofstream&      f,
                                    const BookSnapshot& snap) {
        f << fixed << setprecision(6);
        for (auto& [p, q] : snap.bids)
            f << snap.timestamp.count() << ",BID," << p << "," << q << "\n";
        for (auto& [p, q] : snap.asks)
            f << snap.timestamp.count() << ",ASK," << p << "," << q << "\n";
    }

    static void write_timeseries_header(ofstream& f) {
        f << "timestamp_ns,side,price,quantity\n";
    }

private:
    static ofstream open_write(const filesystem::path& path) {
        ofstream f(path);
        if (!f.is_open())
            throw runtime_error("Exporter: cannot open for writing: " +
                                     path.string());
        return f;
    }
};

} // namespace lob
