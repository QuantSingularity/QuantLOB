#include "lob/FeedHandler.hpp"
#include "lob/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std;

namespace lob {

FeedHandler::FeedHandler(MatchingEngine& engine)
    : engine_(engine) {}

void FeedHandler::configure(const FeedConfig& cfg) {
    config_ = cfg;
    engine_.register_symbol(cfg.symbol);
}

// ---------------------------------------------------------------------------
// LOBSTER CSV parser
//
// Expected column layout (no header row):
//   [0] time        — seconds since midnight (float)
//   [1] event_type  — 1=new, 2=partial-cancel, 3=delete, 4=exec, 5=hidden, 7=halt
//   [2] order_id    — integer
//   [3] size        — integer shares
//   [4] price       — integer, scaled by 10000 (e.g. 1000000 = $100.00)
//   [5] direction   — 1=buy, -1=sell
// ---------------------------------------------------------------------------

vector<LOBSTEREvent> FeedHandler::load_lobster_csv(
    const filesystem::path& message_file) {

    vector<LOBSTEREvent> events;
    ifstream             file(message_file);
    if (!file.is_open())
        throw runtime_error("FeedHandler: cannot open: " +
                                 message_file.string());

    string line;
    size_t line_num      = 0;
    size_t skipped_lines = 0;

    while (getline(file, line)) {
        ++line_num;
        if (line.empty()) continue;

        // Strip trailing CR (Windows line endings).
        if (line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Skip optional CSV header row.
        if (line_num == 1 &&
            (line[0] == 'T' || line[0] == 't' || line[0] == '#'))
            continue;

        istringstream       ss(line);
        string              token;
        vector<string> fields;
        fields.reserve(6);

        while (getline(ss, token, ','))
            fields.push_back(token);

        if (fields.size() < 6) {
            ++skipped_lines;
            LOB_WARN("FeedHandler", "Skipping malformed line " +
                     to_string(line_num) + ": " + line);
            continue;
        }

        try {
            LOBSTEREvent ev;
            double ts_sec    = stod(fields[0]);
            auto   ts_ns     = static_cast<int64_t>(ts_sec * 1e9);
            ev.timestamp     = chrono::nanoseconds{ts_ns};
            ev.event_type    = stoi(fields[1]);
            ev.order_id      = stoull(fields[2]);
            ev.size          = stoull(fields[3]);
            // LOBSTER prices are integers scaled ×10000.
            ev.price         = stod(fields[4]) / 10000.0;
            ev.direction     = stoi(fields[5]);
            events.push_back(ev);
        } catch (const exception& ex) {
            ++skipped_lines;
            LOB_WARN("FeedHandler", "Parse error at line " +
                     to_string(line_num) + ": " + ex.what());
        }
    }

    LOB_INFO("FeedHandler",
             "Loaded " + to_string(events.size()) +
             " events from " + message_file.string() +
             " (skipped " + to_string(skipped_lines) + " lines)");
    return events;
}

// ---------------------------------------------------------------------------
// LOBSTER replay
// ---------------------------------------------------------------------------

void FeedHandler::replay_lobster(const vector<LOBSTEREvent>& events,
                                 const string&               symbol) {
    engine_.register_symbol(symbol);

    chrono::nanoseconds prev_ts{0};

    for (auto& ev : events) {
        if (event_cb_) event_cb_(ev);

        // Optional wall-clock pacing between events.
        if (config_.replay_realtime &&
            config_.replay_speed > 0.0 &&
            prev_ts.count() > 0 &&
            ev.timestamp > prev_ts) {
            auto gap_ns = ev.timestamp - prev_ts;
            auto delay  = chrono::nanoseconds{
                static_cast<int64_t>(
                    static_cast<double>(gap_ns.count()) /
                    config_.replay_speed)};
            if (delay.count() > 0)
                this_thread::sleep_for(delay);
        }
        prev_ts = ev.timestamp;

        switch (ev.event_type) {
            case 1: {
                // New limit order submission.
                Order o = build_order_from_event(ev, symbol);
                if (order_cb_) order_cb_(o);
                engine_.submit_order(o);
                break;
            }
            case 2:
                // Partial cancel / size reduction — new size is ev.size.
                engine_.modify_order(symbol, ev.order_id, ev.size);
                break;
            case 3:
                // Full cancel / deletion.
                engine_.cancel_order(symbol, ev.order_id);
                break;
            case 4:
                // Visible execution (aggressive side removes passive order).
                engine_.cancel_order(symbol, ev.order_id);
                break;
            case 5:
                // Hidden execution — no visible LOB change; skip.
                break;
            case 7:
                LOB_WARN("FeedHandler",
                         "Trading halt at " +
                         to_string(ev.timestamp.count()) + " ns");
                break;
            default:
                LOB_DEBUG("FeedHandler",
                          "Unknown LOBSTER event type: " +
                          to_string(ev.event_type));
                break;
        }

        ++events_processed_;
    }

    LOB_INFO("FeedHandler",
             "Replay complete: " + to_string(events_processed_) +
             " events processed for " + symbol);
}

// ---------------------------------------------------------------------------
// Synthetic generation
//
// Models a simple, mean-reverting LOB:
//   • Poisson inter-arrival times.
//   • Orders placed within a few ticks of the current mid.
//   • Cancel events drawn with probability cancel_rate.
//   • Mid price follows a small random walk driven by executed trades.
// ---------------------------------------------------------------------------

void FeedHandler::generate_synthetic(const SyntheticConfig& cfg) {
    mt19937_64                         rng{cfg.seed};
    exponential_distribution<double>   inter_arrival{cfg.arrival_rate};
    normal_distribution<double>        price_noise{0.0, cfg.price_std};
    uniform_int_distribution<uint64_t> qty_dist{cfg.min_qty, cfg.max_qty};
    uniform_real_distribution<double>  uni{0.0, 1.0};
    uniform_int_distribution<int>      level_dist{0, cfg.spread_ticks + 2};

    const string& symbol = config_.symbol;
    double             mid    = cfg.mid_price;

    // active_ids tracks resting order IDs for random cancellation.
    // Swap-and-pop gives O(1) removal.
    vector<uint64_t> active_ids;
    active_ids.reserve(2048);

    chrono::nanoseconds ts{0};

    for (uint64_t i = 0; i < cfg.num_events; ++i) {
        ts += chrono::nanoseconds{
            static_cast<int64_t>(inter_arrival(rng) * 1e9)};

        // --- Cancel event ---
        if (!active_ids.empty() && uni(rng) < cfg.cancel_rate) {
            uniform_int_distribution<size_t> idx_dist{
                0, active_ids.size() - 1};
            size_t idx = idx_dist(rng);
            uint64_t    cid = active_ids[idx];

            // Remove from active_ids regardless of whether the cancel
            // succeeds (the order may have been matched already).
            active_ids[idx] = active_ids.back();
            active_ids.pop_back();

            engine_.cancel_order(symbol, cid);
            ++events_processed_;
            continue;
        }

        // --- New limit order ---
        Side side = (uni(rng) < 0.5) ? Side::BUY : Side::SELL;

        // Place within a few ticks of the mid.  abs ensures we always
        // move away from mid (no crossed orders from the generator itself).
        double offset = static_cast<double>(level_dist(rng)) * cfg.tick_size +
                        abs(price_noise(rng));
        double raw_price = (side == Side::BUY) ? mid - offset : mid + offset;

        // Snap to tick grid and enforce a positive minimum.
        double price = round(raw_price / cfg.tick_size) * cfg.tick_size;
        price        = max(price, cfg.tick_size);

        uint64_t qty = qty_dist(rng);
        uint64_t oid = next_order_id_++;

        Order o{oid, side, OrderType::LIMIT, price, qty, symbol, ts};

        if (order_cb_) order_cb_(o);

        auto result = engine_.submit_order(o);

        if (result.resting)
            active_ids.push_back(oid);

        // Apply a small mean-reversion to the mid based on the last trade.
        if (!result.trades.empty()) {
            double last_price = result.trades.back().price;
            // Exponential moving average towards the traded price, plus noise.
            mid = mid + 0.05 * (last_price - mid) +
                  price_noise(rng) * cfg.tick_size * 0.5;
            mid = max(mid, cfg.tick_size);
        }

        ++events_processed_;
    }
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void FeedHandler::set_event_callback(EventCallback cb) {
    event_cb_ = move(cb);
}

void FeedHandler::set_order_callback(OrderCallback cb) {
    order_cb_ = move(cb);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Order FeedHandler::build_order_from_event(const LOBSTEREvent& ev,
                                          const string&  symbol) const {
    // LOBSTER direction: 1 = buy, -1 = sell.
    Side side = (ev.direction == 1) ? Side::BUY : Side::SELL;
    return Order{ev.order_id, side, OrderType::LIMIT,
                 ev.price, ev.size, symbol, ev.timestamp};
}

} // namespace lob
