#pragma once

#include "Order.hpp"

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace lob {

/// A single price level: holds the aggregate quantity and a FIFO list of
/// order IDs resting at that price.
struct PriceLevel {
    double              price;
    uint64_t            total_quantity;
    list<uint64_t> order_ids;  ///< front = oldest (FIFO priority)

    explicit PriceLevel(double p) noexcept : price(p), total_quantity(0) {}
};

/// Lightweight snapshot of the top N levels of the book.
struct BookSnapshot {
    string                              symbol;
    vector<pair<double, uint64_t>> bids;  ///< descending price
    vector<pair<double, uint64_t>> asks;  ///< ascending price
    chrono::nanoseconds                 timestamp;
};

/// Volume-weighted average price over the top N levels on one side.
struct VWAPResult {
    double   vwap;         ///< 0.0 if no liquidity
    uint64_t total_qty;    ///< total quantity available
    bool     valid;        ///< false if book side is empty
};

class OrderBook {
public:
    explicit OrderBook(string symbol);
    ~OrderBook() noexcept = default;

    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // ------------------------------------------------------------------
    // Mutation
    // ------------------------------------------------------------------

    /// Add a resting order. Returns false if order_id already exists.
    bool add_order(Order order);

    /// Cancel a resting order. Returns false if not found or not active.
    bool cancel_order(uint64_t order_id);

    /// Modify the total quantity of an existing active order.
    /// new_quantity must be strictly greater than filled_quantity.
    /// Quantity reductions preserve time-priority; increases lose priority
    /// on most exchanges — callers should cancel+resubmit for increases if
    /// strict exchange semantics are required.
    bool modify_order(uint64_t order_id, uint64_t new_quantity);

    /// Remove ALL resting orders and clear all internal state.
    void reset() noexcept;

    // ------------------------------------------------------------------
    // Book statistics
    // ------------------------------------------------------------------

    [[nodiscard]] optional<double> best_bid()  const noexcept;
    [[nodiscard]] optional<double> best_ask()  const noexcept;
    [[nodiscard]] optional<double> mid_price() const noexcept;
    [[nodiscard]] optional<double> spread()    const noexcept;

    /// Spread as a fraction of mid-price (0.0 if mid unavailable).
    [[nodiscard]] optional<double> relative_spread() const noexcept;

    [[nodiscard]] uint64_t    bid_depth()        const noexcept;
    [[nodiscard]] uint64_t    ask_depth()        const noexcept;
    [[nodiscard]] size_t order_count()      const noexcept;
    [[nodiscard]] size_t level_count_bids() const noexcept;
    [[nodiscard]] size_t level_count_asks() const noexcept;

    /// Order-book imbalance: (bid_depth - ask_depth) / (bid_depth + ask_depth).
    /// Returns 0.0 if both sides are empty.
    [[nodiscard]] double imbalance() const noexcept;

    /// VWAP of the top `levels` price levels on the bid side.
    [[nodiscard]] VWAPResult bid_vwap(size_t levels = 5) const noexcept;

    /// VWAP of the top `levels` price levels on the ask side.
    [[nodiscard]] VWAPResult ask_vwap(size_t levels = 5) const noexcept;

    /// Cumulative quantity available at or better than `price` on the given side.
    [[nodiscard]] uint64_t available_qty_at_price(Side side, double price) const noexcept;

    /// Estimated average execution price for a market order of `qty` shares.
    /// Returns nullopt if insufficient liquidity in the book.
    [[nodiscard]] optional<double> estimate_market_impact(Side     side,
                                                               uint64_t qty) const noexcept;

    /// Top-N snapshot (bids descending, asks ascending).
    [[nodiscard]] BookSnapshot snapshot(size_t levels = 5) const;

    [[nodiscard]] const string& symbol()     const noexcept { return symbol_; }
    [[nodiscard]] const Order*       find_order(uint64_t order_id) const noexcept;

    // ------------------------------------------------------------------
    // Internals exposed to MatchingEngine
    // ------------------------------------------------------------------
    using BidMap = map<double, PriceLevel, greater<double>>;
    using AskMap = map<double, PriceLevel, less<double>>;

    BidMap&       bids()       noexcept { return bids_; }
    AskMap&       asks()       noexcept { return asks_; }
    const BidMap& bids() const noexcept { return bids_; }
    const AskMap& asks() const noexcept { return asks_; }

    unordered_map<uint64_t, Order>&       orders()       noexcept { return orders_; }
    const unordered_map<uint64_t, Order>& orders() const noexcept { return orders_; }

    /// Deduct fill_qty from the passive order's filled_quantity and from the
    /// level's total_quantity.  Called by MatchingEngine::cross() for partial
    /// and full fills.
    void apply_fill(uint64_t order_id, uint64_t fill_qty);

    /// Remove a fully-consumed passive order from its price level's order_ids
    /// list AND erase it from orders_.  Called by cross() after a full fill.
    void remove_fully_filled(uint64_t order_id);

private:
    void remove_from_level_internal(Side     side,
                                    double   price,
                                    uint64_t order_id,
                                    uint64_t qty) noexcept;

    string                         symbol_;
    BidMap                              bids_;
    AskMap                              asks_;
    unordered_map<uint64_t, Order> orders_;
};

} // namespace lob
