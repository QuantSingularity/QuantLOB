#pragma once

#include "lob/OrderBook.hpp"
#include "lob/Order.hpp"

#include <cstddef>
#include <deque>
#include <vector>

namespace lob {
namespace ai_models {

/// Number of elements in every FeatureVector produced by FeatureExtractor.
static constexpr std::size_t FEATURE_DIM = 40;

/// A feature vector plus a validity flag that is false during the warm-up window.
struct FeatureVector {
    std::vector<double> data;
    bool                valid{false};

    FeatureVector() : data(FEATURE_DIM, 0.0) {}
};

/// Compact record of one recent trade for order-flow features.
struct TradeRecord {
    double   price;
    uint64_t quantity;
    int      direction; ///< +1 = buy-initiated, -1 = sell-initiated
};

/// Converts a BookSnapshot plus rolling history into a fixed-length
/// feature vector.  See docs/05_ml_module.md for the full feature layout.
class FeatureExtractor {
public:
    /// @param window        Mid-price history length for momentum and volatility.
    /// @param trade_window  Recent trade records used for order-flow features.
    explicit FeatureExtractor(int window = 50, int trade_window = 20);

    /// Extract the current feature vector from a snapshot.
    /// Returns valid=false until the history is filled.
    [[nodiscard]] FeatureVector extract(const BookSnapshot& snap) const;

    /// Push a new mid-price observation.  Call once per tick before extract().
    void push_mid(double mid);

    /// Record a trade for order-flow features.
    void push_trade(double price, uint64_t qty, int direction);

    /// Clear all history.
    void reset() noexcept;

    [[nodiscard]] int  window()       const noexcept { return window_; }
    [[nodiscard]] int  trade_window() const noexcept { return trade_window_; }
    [[nodiscard]] bool is_ready()     const noexcept;

private:
    int window_;
    int trade_window_;

    std::deque<double>      mid_history_;
    std::deque<TradeRecord> trade_history_;

    static double safe_div(double num, double den) noexcept {
        return (den == 0.0) ? 0.0 : num / den;
    }
};

} // namespace ai_models
} // namespace lob
