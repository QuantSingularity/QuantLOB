#pragma once

#include "lob/MatchingEngine.hpp"
#include "lob/OrderBook.hpp"
#include "lob/Order.hpp"
#include "lob/Latency.hpp"
#include "lob/ai_models/MLPipeline.hpp"

#include <nlohmann/json.hpp>

#include <vector>

using namespace std;

namespace lob {

using nlohmann::json;

// Top-of-book metrics plus the depth ladder for one symbol.
inline json snapshot_to_json(const OrderBook& book, size_t levels) {
    const auto snap = book.snapshot(levels);

    json bids = json::array();
    for (const auto& [price, qty] : snap.bids) {
        bids.push_back(json{{"price", price}, {"quantity", qty}});
    }
    json asks = json::array();
    for (const auto& [price, qty] : snap.asks) {
        asks.push_back(json{{"price", price}, {"quantity", qty}});
    }

    const auto bid    = book.best_bid();
    const auto ask    = book.best_ask();
    const auto mid    = book.mid_price();
    const auto spread = book.spread();
    const auto rspr   = book.relative_spread();
    const auto bvwap  = book.bid_vwap(levels);
    const auto avwap  = book.ask_vwap(levels);

    json j;
    j["symbol"]          = book.symbol();
    j["bids"]            = bids;
    j["asks"]            = asks;
    j["best_bid"]        = bid ? json(*bid) : json(nullptr);
    j["best_ask"]        = ask ? json(*ask) : json(nullptr);
    j["mid_price"]       = mid ? json(*mid) : json(nullptr);
    j["spread"]          = spread ? json(*spread) : json(nullptr);
    j["relative_spread"] = rspr ? json(*rspr) : json(nullptr);
    j["imbalance"]       = book.imbalance();
    j["bid_depth"]       = book.bid_depth();
    j["ask_depth"]       = book.ask_depth();
    j["order_count"]     = book.order_count();
    j["bid_levels"]      = book.level_count_bids();
    j["ask_levels"]      = book.level_count_asks();
    j["bid_vwap"]        = bvwap.valid ? json(bvwap.vwap) : json(nullptr);
    j["ask_vwap"]        = avwap.valid ? json(avwap.vwap) : json(nullptr);
    return j;
}

inline json stats_to_json(const EngineStats& s) {
    return json{
        {"orders_processed", s.orders_processed},
        {"orders_matched", s.orders_matched},
        {"orders_resting", s.orders_resting},
        {"orders_cancelled", s.orders_cancelled},
        {"orders_rejected", s.orders_rejected},
        {"total_trades", s.total_trades},
        {"total_volume", s.total_volume},
        {"total_notional", s.total_notional},
    };
}

inline json trade_to_json(const Trade& t) {
    return json{
        {"buy_order_id", t.buy_order_id},
        {"sell_order_id", t.sell_order_id},
        {"price", t.price},
        {"quantity", t.quantity},
        {"timestamp_ns", static_cast<long long>(t.timestamp.count())},
    };
}

inline json match_result_to_json(const MatchResult& r) {
    json trades = json::array();
    for (const auto& t : r.trades) {
        trades.push_back(trade_to_json(t));
    }
    return json{
        {"trades", trades},
        {"resting", r.resting},
        {"fully_filled", r.fully_filled},
        {"rejected", r.rejected},
        {"reject_reason", r.reject_reason},
    };
}

inline json ml_to_json(const ai_models::PipelineResult& r) {
    return json{
        {"mid_price_forecast", r.mid_price_forecast},
        {"buy_probability", r.buy_probability},
        {"anomaly_score", r.anomaly_score},
        {"anomaly_features", r.anomaly_features},
        {"is_anomaly", r.is_anomaly},
        {"model_ready", r.model_ready},
    };
}

inline json latency_to_json(const LatencyRecorder& rec) {
    if (rec.count() == 0) {
        return json{{"samples", 0}};
    }
    return json{
        {"samples", rec.count()},
        {"mean_ns", rec.mean_ns()},
        {"stddev_ns", rec.stddev_ns()},
        {"min_ns", rec.min_ns()},
        {"max_ns", rec.max_ns()},
        {"p50_ns", rec.p50_ns()},
        {"p90_ns", rec.p90_ns()},
        {"p99_ns", rec.p99_ns()},
        {"p999_ns", rec.p999_ns()},
    };
}

} // namespace lob
