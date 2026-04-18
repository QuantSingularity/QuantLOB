#include "lob/ai_models/FeatureExtractor.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lob {
namespace ai_models {

FeatureExtractor::FeatureExtractor(int window, int trade_window)
    : window_(window), trade_window_(trade_window) {}

void FeatureExtractor::push_mid(double mid) {
    mid_history_.push_back(mid);
    if (static_cast<int>(mid_history_.size()) > window_)
        mid_history_.pop_front();
}

void FeatureExtractor::push_trade(double price, uint64_t qty, int direction) {
    trade_history_.push_back({price, qty, direction});
    if (static_cast<int>(trade_history_.size()) > trade_window_)
        trade_history_.pop_front();
}

void FeatureExtractor::reset() noexcept {
    mid_history_.clear();
    trade_history_.clear();
}

bool FeatureExtractor::is_ready() const noexcept {
    return static_cast<int>(mid_history_.size()) >= window_;
}

FeatureVector FeatureExtractor::extract(const BookSnapshot& snap) const {
    FeatureVector fv;
    fv.valid = is_ready();

    auto bid_price = [&](int lvl) -> double {
        return (lvl < static_cast<int>(snap.bids.size())) ? snap.bids[lvl].first : 0.0;
    };
    auto bid_qty = [&](int lvl) -> double {
        return (lvl < static_cast<int>(snap.bids.size()))
               ? static_cast<double>(snap.bids[lvl].second) : 0.0;
    };
    auto ask_price = [&](int lvl) -> double {
        return (lvl < static_cast<int>(snap.asks.size())) ? snap.asks[lvl].first : 0.0;
    };
    auto ask_qty = [&](int lvl) -> double {
        return (lvl < static_cast<int>(snap.asks.size()))
               ? static_cast<double>(snap.asks[lvl].second) : 0.0;
    };

    double b1  = bid_price(0);
    double a1  = ask_price(0);
    double mid = (b1 > 0.0 && a1 > 0.0) ? (b1 + a1) * 0.5 : 0.0;

    // -------------------------------------------------------------------
    // Features 0-1: spread (absolute and relative)
    // -------------------------------------------------------------------
    fv.data[0] = (a1 > 0.0 && b1 > 0.0) ? a1 - b1 : 0.0;
    fv.data[1] = (mid > 0.0) ? fv.data[0] / mid : 0.0;

    // -------------------------------------------------------------------
    // Features 2-3: mid-price change and log return
    // -------------------------------------------------------------------
    double prev_mid = (mid_history_.size() >= 2)
                      ? mid_history_[mid_history_.size() - 2]
                      : mid;
    fv.data[2] = (mid > 0.0 && prev_mid > 0.0) ? (mid - prev_mid) / prev_mid : 0.0;
    fv.data[3] = (prev_mid > 0.0 && mid > 0.0) ? std::log(mid / prev_mid) : 0.0;

    // -------------------------------------------------------------------
    // Features 4-6: order-book imbalance at 1, 2, and 3 levels
    // All naturally in [-1, 1].
    // -------------------------------------------------------------------
    for (int n = 1; n <= 3; ++n) {
        double bd = 0.0, ad = 0.0;
        for (int i = 0; i < n; ++i) { bd += bid_qty(i); ad += ask_qty(i); }
        fv.data[3 + n] = safe_div(bd - ad, bd + ad);
    }

    // -------------------------------------------------------------------
    // Features 7-9: depth features — normalised to [-1, 1] / bounded
    //
    // Raw bid/ask depths can be in the thousands and cause SGD divergence
    // when used directly as features.  We normalise each by the total
    // two-sided depth so the values stay in [0, 1], and express the ratio
    // as imbalance-style (already bounded).
    // -------------------------------------------------------------------
    double total_bid = 0.0, total_ask = 0.0;
    for (int i = 0; i < 6; ++i) { total_bid += bid_qty(i); total_ask += ask_qty(i); }
    double total_depth = total_bid + total_ask;

    // Feature 7: bid-side fraction of total depth  -> [0, 1]
    fv.data[7] = safe_div(total_bid, total_depth);
    // Feature 8: ask-side fraction of total depth  -> [0, 1]
    fv.data[8] = safe_div(total_ask, total_depth);
    // Feature 9: depth imbalance (bid - ask) / total -> [-1, 1]
    fv.data[9] = safe_div(total_bid - total_ask, total_depth);

    // -------------------------------------------------------------------
    // Features 10-11: VWAP over top 5 levels, normalised by mid
    // Expressed as deviation from mid: (vwap - mid) / mid -> ~[-0.1, 0.1]
    // -------------------------------------------------------------------
    {
        double bw = 0.0, bq = 0.0, aw = 0.0, aq = 0.0;
        for (int i = 0; i < 5; ++i) {
            bw += bid_price(i) * bid_qty(i); bq += bid_qty(i);
            aw += ask_price(i) * ask_qty(i); aq += ask_qty(i);
        }
        double bid_vwap = (bq > 0.0) ? bw / bq : b1;
        double ask_vwap = (aq > 0.0) ? aw / aq : a1;
        fv.data[10] = (mid > 0.0) ? (bid_vwap - mid) / mid : 0.0;
        fv.data[11] = (mid > 0.0) ? (ask_vwap - mid) / mid : 0.0;
    }

    // -------------------------------------------------------------------
    // Feature 12: signed trade flow — normalised by window size so it
    // stays bounded regardless of order sizes.
    // -------------------------------------------------------------------
    {
        double flow = 0.0;
        for (auto& t : trade_history_)
            flow += static_cast<double>(t.quantity) * t.direction;
        // Normalise: divide by (trade_window * a typical max qty of 1000)
        // to keep the value in approximately [-1, 1].
        double scale = static_cast<double>(trade_window_) * 1000.0;
        fv.data[12] = (scale > 0.0) ? flow / scale : 0.0;
        // Clamp to [-1, 1] as a safety net
        fv.data[12] = std::max(-1.0, std::min(1.0, fv.data[12]));
    }

    // -------------------------------------------------------------------
    // Feature 13: trade intensity — fraction of window filled -> [0, 1]
    // -------------------------------------------------------------------
    fv.data[13] = safe_div(static_cast<double>(trade_history_.size()),
                           static_cast<double>(trade_window_));

    // -------------------------------------------------------------------
    // Feature 14: price momentum — log return over full window, clamped
    // -------------------------------------------------------------------
    if (mid_history_.size() >= 2) {
        double first = mid_history_.front();
        double last  = mid_history_.back();
        fv.data[14] = (first > 0.0 && last > 0.0) ? std::log(last / first) : 0.0;
    }

    // -------------------------------------------------------------------
    // Feature 15: rolling volatility (std of log-returns), already small
    // -------------------------------------------------------------------
    if (mid_history_.size() >= 3) {
        std::vector<double> rets;
        rets.reserve(mid_history_.size() - 1);
        for (std::size_t i = 1; i < mid_history_.size(); ++i) {
            double m0 = mid_history_[i - 1];
            double m1 = mid_history_[i];
            if (m0 > 0.0 && m1 > 0.0)
                rets.push_back(std::log(m1 / m0));
        }
        if (!rets.empty()) {
            double mean = std::accumulate(rets.begin(), rets.end(), 0.0) /
                          static_cast<double>(rets.size());
            double var = 0.0;
            for (double r : rets) var += (r - mean) * (r - mean);
            fv.data[15] = std::sqrt(var / static_cast<double>(rets.size()));
        }
    }

    // -------------------------------------------------------------------
    // Features 16-21: per-level bid quantity fractions -> [0, 1]
    // Normalised by total_bid so the six levels sum to 1.
    // -------------------------------------------------------------------
    for (int i = 0; i < 6; ++i)
        fv.data[16 + i] = safe_div(bid_qty(i), total_bid > 0.0 ? total_bid : 1.0);

    // -------------------------------------------------------------------
    // Features 22-27: per-level ask quantity fractions -> [0, 1]
    // -------------------------------------------------------------------
    for (int i = 0; i < 6; ++i)
        fv.data[22 + i] = safe_div(ask_qty(i), total_ask > 0.0 ? total_ask : 1.0);

    // -------------------------------------------------------------------
    // Features 28-33: per-level bid price deviations from mid -> small
    // -------------------------------------------------------------------
    for (int i = 0; i < 6; ++i)
        fv.data[28 + i] = (mid > 0.0) ? safe_div(bid_price(i) - mid, mid) : 0.0;

    // -------------------------------------------------------------------
    // Features 34-39: per-level ask price deviations from mid -> small
    // -------------------------------------------------------------------
    for (int i = 0; i < 6; ++i)
        fv.data[34 + i] = (mid > 0.0) ? safe_div(ask_price(i) - mid, mid) : 0.0;

    return fv;
}

} // namespace ai_models
} // namespace lob
