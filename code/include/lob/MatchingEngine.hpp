#pragma once

#include "Order.hpp"
#include "OrderBook.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace lob {

/// Result returned by every submit_order() call.
struct MatchResult {
    vector<Trade> trades;
    bool               resting;        ///< order (or partial remainder) rested on the book
    bool               fully_filled;   ///< aggressor was 100 % consumed
    bool               rejected;       ///< order was rejected (e.g. FOK unfilled)
    string        reject_reason;
};

using TradeCallback  = function<void(const Trade&)>;
using RejectCallback = function<void(uint64_t order_id, const string& reason)>;
using FillCallback   = function<void(uint64_t order_id, uint64_t fill_qty, double price)>;

/// Cumulative engine statistics (reset via reset_stats()).
struct EngineStats {
    uint64_t orders_processed  = 0;
    uint64_t orders_matched    = 0;   ///< fully filled aggressors
    uint64_t orders_resting    = 0;   ///< aggressors that rested (at least partially)
    uint64_t orders_cancelled  = 0;
    uint64_t orders_rejected   = 0;
    uint64_t total_trades      = 0;
    uint64_t total_volume      = 0;   ///< total shares/contracts traded
    double   total_notional    = 0.0; ///< total value traded (Σ price × qty)
};

class MatchingEngine {
public:
    MatchingEngine();
    ~MatchingEngine() noexcept = default;

    MatchingEngine(const MatchingEngine&)            = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    // ------------------------------------------------------------------
    // Symbol registration
    // ------------------------------------------------------------------

    /// Explicitly register a symbol before first use.  submit_order() will
    /// auto-register unknown symbols, but pre-registration avoids the extra
    /// map lookup on the hot path.
    void register_symbol(const string& symbol);

    /// Returns true if the symbol has a registered book.
    [[nodiscard]] bool has_symbol(const string& symbol) const noexcept;

    // ------------------------------------------------------------------
    // Order lifecycle
    // ------------------------------------------------------------------

    MatchResult submit_order(Order order);
    bool        cancel_order(const string& symbol, uint64_t order_id);

    /// Modify the quantity of a resting order.
    /// new_quantity must exceed filled_quantity; otherwise returns false.
    bool modify_order(const string& symbol,
                      uint64_t           order_id,
                      uint64_t           new_quantity);

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------
    void set_trade_callback(TradeCallback cb);
    void set_reject_callback(RejectCallback cb);

    /// Per-fill callback fires for BOTH sides of each trade
    /// (passive fill + aggressor partial fill).
    void set_fill_callback(FillCallback cb);

    // ------------------------------------------------------------------
    // Inspection
    // ------------------------------------------------------------------
    [[nodiscard]] const OrderBook*   get_book(const string& symbol) const noexcept;
    [[nodiscard]] const EngineStats& stats() const noexcept { return stats_; }

    void reset_stats() noexcept;

    /// Reset a symbol's order book (e.g. start-of-day / session reset).
    void reset_book(const string& symbol);

    /// Reset every registered book.
    void reset_all_books() noexcept;

private:
    // ------------------------------------------------------------------
    // Internal matching routines
    // ------------------------------------------------------------------
    MatchResult match_limit(Order& order, OrderBook& book);
    MatchResult match_market(Order& order, OrderBook& book);
    MatchResult match_ioc(Order& order, OrderBook& book);
    MatchResult match_fok(Order& order, OrderBook& book);

    /// Execute price-time priority crossing of aggressor against the opposing
    /// side of the book.  Returns all trades generated.
    vector<Trade> cross(Order& aggressor, OrderBook& book);

    Trade make_trade(uint64_t buy_id,
                     uint64_t sell_id,
                     double   price,
                     uint64_t qty) const noexcept;

    unordered_map<string, OrderBook> books_;
    TradeCallback                              trade_cb_;
    RejectCallback                             reject_cb_;
    FillCallback                               fill_cb_;
    EngineStats                                stats_;
};

} // namespace lob
