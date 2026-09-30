#include "lob/MatchingEngine.hpp"
#include "lob/Logger.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace lob {

MatchingEngine::MatchingEngine() {
    stats_ = {};
}

// ---------------------------------------------------------------------------
// Symbol registration
// ---------------------------------------------------------------------------

void MatchingEngine::register_symbol(const string& symbol) {
    auto [it, inserted] = books_.try_emplace(symbol, symbol);
    if (inserted)
        LOB_INFO("MatchingEngine", "Registered symbol: " + symbol);
}

bool MatchingEngine::has_symbol(const string& symbol) const noexcept {
    return books_.find(symbol) != books_.end();
}

// ---------------------------------------------------------------------------
// Order lifecycle
// ---------------------------------------------------------------------------

MatchResult MatchingEngine::submit_order(Order order) {
    // Auto-register unknown symbols so callers don't have to pre-register.
    auto [it, inserted] = books_.try_emplace(order.symbol, order.symbol);
    if (inserted)
        LOB_INFO("MatchingEngine", "Auto-registered symbol: " + order.symbol);

    OrderBook& book = it->second;
    ++stats_.orders_processed;

    MatchResult result;
    switch (order.type) {
        case OrderType::LIMIT:  result = match_limit(order, book);  break;
        case OrderType::MARKET: result = match_market(order, book); break;
        case OrderType::IOC:    result = match_ioc(order, book);    break;
        case OrderType::FOK:    result = match_fok(order, book);    break;
    }

    // Accumulate engine-level statistics.
    stats_.total_trades += result.trades.size();
    for (auto& t : result.trades) {
        stats_.total_volume   += t.quantity;
        stats_.total_notional += t.price * static_cast<double>(t.quantity);
    }

    if (result.fully_filled) ++stats_.orders_matched;
    if (result.resting)      ++stats_.orders_resting;
    if (result.rejected)     ++stats_.orders_rejected;

    // Fire callbacks.
    for (auto& t : result.trades) {
        if (trade_cb_) trade_cb_(t);
        if (fill_cb_) {
            fill_cb_(t.buy_order_id,  t.quantity, t.price);
            fill_cb_(t.sell_order_id, t.quantity, t.price);
        }
    }

    if (result.rejected && reject_cb_)
        reject_cb_(order.id, result.reject_reason);

    return result;
}

bool MatchingEngine::cancel_order(const string& symbol, uint64_t order_id) {
    auto it = books_.find(symbol);
    if (it == books_.end()) return false;
    bool ok = it->second.cancel_order(order_id);
    if (ok) ++stats_.orders_cancelled;
    return ok;
}

bool MatchingEngine::modify_order(const string& symbol,
                                  uint64_t           order_id,
                                  uint64_t           new_quantity) {
    auto it = books_.find(symbol);
    if (it == books_.end()) return false;
    return it->second.modify_order(order_id, new_quantity);
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void MatchingEngine::set_trade_callback(TradeCallback cb) {
    trade_cb_ = move(cb);
}

void MatchingEngine::set_reject_callback(RejectCallback cb) {
    reject_cb_ = move(cb);
}

void MatchingEngine::set_fill_callback(FillCallback cb) {
    fill_cb_ = move(cb);
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

const OrderBook* MatchingEngine::get_book(const string& symbol) const noexcept {
    auto it = books_.find(symbol);
    if (it == books_.end()) return nullptr;
    return &it->second;
}

void MatchingEngine::reset_stats() noexcept {
    stats_ = {};
}

void MatchingEngine::reset_book(const string& symbol) {
    auto it = books_.find(symbol);
    if (it != books_.end()) {
        it->second.reset();
        LOB_INFO("MatchingEngine", "Reset book: " + symbol);
    }
}

void MatchingEngine::reset_all_books() noexcept {
    for (auto& [sym, book] : books_)
        book.reset();
}

// ---------------------------------------------------------------------------
// Core price-time priority crossing
//
// Walks the opposing side's price levels in priority order (ascending for
// asks, descending for bids).  Within each level orders are served FIFO.
//
// Design invariant maintained here:
//   • apply_fill()       - updates filled_quantity / status AND level total_quantity.
//   • Cross() itself    - removes the oid from order_ids (via list erase) and
//                         removes the Order record from orders_ map after a
//                         full fill so that find_order / order_count stay
//                         consistent without a separate remove_fully_filled call.
//   • Level erasure     - happens only after order_ids is confirmed empty.
// ---------------------------------------------------------------------------

vector<Trade> MatchingEngine::cross(Order& aggressor, OrderBook& book) {
    vector<Trade> trades;

    if (aggressor.side == Side::BUY) {
        auto& asks = book.asks();
        auto  lvl  = asks.begin();

        while (aggressor.remaining() > 0 && lvl != asks.end()) {
            // Limit price check: only cross if ask_price <= aggressor limit.
            if (aggressor.type == OrderType::LIMIT &&
                lvl->first > aggressor.price)
                break;

            auto& order_ids = lvl->second.order_ids;
            auto  oid_it    = order_ids.begin();

            while (aggressor.remaining() > 0 && oid_it != order_ids.end()) {
                const uint64_t passive_id = *oid_it;
                auto           pit        = book.orders().find(passive_id);

                // Stale reference guard - should not occur in normal operation.
                if (pit == book.orders().end()) {
                    oid_it = order_ids.erase(oid_it);
                    continue;
                }

                Order&   passive  = pit->second;
                uint64_t fill_qty = min(aggressor.remaining(),
                                             passive.remaining());
                if (fill_qty == 0) { ++oid_it; continue; }

                // Passive price priority: match at the resting ask price.
                Trade t = make_trade(aggressor.id, passive.id,
                                     passive.price, fill_qty);
                trades.push_back(t);

                // Update aggressor in-place.
                aggressor.filled_quantity += fill_qty;
                aggressor.status = (aggressor.remaining() == 0)
                                   ? OrderStatus::FILLED
                                   : OrderStatus::PARTIAL;

                if (passive.remaining() == fill_qty) {
                    // Passive fully consumed.
                    book.apply_fill(passive_id, fill_qty);
                    oid_it = order_ids.erase(oid_it);
                    book.orders().erase(pit);
                } else {
                    // Passive partially filled - stays on book.
                    book.apply_fill(passive_id, fill_qty);
                    ++oid_it;
                }
            }

            // Erase the level if all orders were consumed.
            if (order_ids.empty())
                lvl = asks.erase(lvl);
            else
                ++lvl;
        }

    } else {
        // SELL aggressor crosses against the bid side (descending price).
        auto& bids = book.bids();
        auto  lvl  = bids.begin();

        while (aggressor.remaining() > 0 && lvl != bids.end()) {
            if (aggressor.type == OrderType::LIMIT &&
                lvl->first < aggressor.price)
                break;

            auto& order_ids = lvl->second.order_ids;
            auto  oid_it    = order_ids.begin();

            while (aggressor.remaining() > 0 && oid_it != order_ids.end()) {
                const uint64_t passive_id = *oid_it;
                auto           pit        = book.orders().find(passive_id);

                if (pit == book.orders().end()) {
                    oid_it = order_ids.erase(oid_it);
                    continue;
                }

                Order&   passive  = pit->second;
                uint64_t fill_qty = min(aggressor.remaining(),
                                             passive.remaining());
                if (fill_qty == 0) { ++oid_it; continue; }

                // Passive bid price priority.
                Trade t = make_trade(passive.id, aggressor.id,
                                     passive.price, fill_qty);
                trades.push_back(t);

                aggressor.filled_quantity += fill_qty;
                aggressor.status = (aggressor.remaining() == 0)
                                   ? OrderStatus::FILLED
                                   : OrderStatus::PARTIAL;

                if (passive.remaining() == fill_qty) {
                    book.apply_fill(passive_id, fill_qty);
                    oid_it = order_ids.erase(oid_it);
                    book.orders().erase(pit);
                } else {
                    book.apply_fill(passive_id, fill_qty);
                    ++oid_it;
                }
            }

            if (order_ids.empty())
                lvl = bids.erase(lvl);
            else
                ++lvl;
        }
    }

    return trades;
}

// ---------------------------------------------------------------------------
// Order type handlers
// ---------------------------------------------------------------------------

// LIMIT: cross then rest any remainder.
MatchResult MatchingEngine::match_limit(Order& order, OrderBook& book) {
    MatchResult result;
    result.resting      = false;
    result.fully_filled = false;
    result.rejected     = false;

    result.trades   = cross(order, book);
    result.fully_filled = (order.remaining() == 0);

    if (!result.fully_filled) {
        book.add_order(order);
        result.resting = true;
    }

    return result;
}

// MARKET: cross with no price constraint; any unfilled remainder is discarded.
MatchResult MatchingEngine::match_market(Order& order, OrderBook& book) {
    MatchResult result;
    result.resting      = false;
    result.fully_filled = false;
    result.rejected     = false;

    // Market orders bypass the limit-price guard inside cross().
    result.trades   = cross(order, book);
    result.fully_filled = (order.remaining() == 0);

    // Unfilled portion is silently discarded - market orders never rest.
    if (!result.fully_filled)
        order.status = OrderStatus::PARTIAL;

    return result;
}

// IOC: cross then immediately cancel any unfilled remainder.
MatchResult MatchingEngine::match_ioc(Order& order, OrderBook& book) {
    MatchResult result;
    result.resting      = false;
    result.fully_filled = false;
    result.rejected     = false;

    result.trades   = cross(order, book);
    result.fully_filled = (order.remaining() == 0);

    if (!result.fully_filled)
        order.status = OrderStatus::CANCELLED;

    return result;
}

// FOK: verify sufficient liquidity exists before touching the book.
//      If not enough, reject without executing any trade.
MatchResult MatchingEngine::match_fok(Order& order, OrderBook& book) {
    MatchResult result;
    result.resting      = false;
    result.fully_filled = false;
    result.rejected     = false;

    // Count available liquidity at or better than the limit price.
    uint64_t available = 0;
    if (order.side == Side::BUY) {
        for (auto& [p, lvl] : book.asks()) {
            if (order.type == OrderType::LIMIT && p > order.price) break;
            available += lvl.total_quantity;
            if (available >= order.quantity) break;
        }
    } else {
        for (auto& [p, lvl] : book.bids()) {
            if (order.type == OrderType::LIMIT && p < order.price) break;
            available += lvl.total_quantity;
            if (available >= order.quantity) break;
        }
    }

    if (available < order.quantity) {
        result.rejected      = true;
        result.reject_reason = "FOK: insufficient liquidity (" +
                               to_string(available) + " available, " +
                               to_string(order.quantity) + " required)";
        order.status = OrderStatus::REJECTED;
        return result;
    }

    result.trades       = cross(order, book);
    result.fully_filled = (order.remaining() == 0);
    return result;
}

// ---------------------------------------------------------------------------
// Trade factory
// ---------------------------------------------------------------------------

Trade MatchingEngine::make_trade(uint64_t buy_id,
                                 uint64_t sell_id,
                                 double   price,
                                 uint64_t qty) const noexcept {
    Trade t;
    t.buy_order_id  = buy_id;
    t.sell_order_id = sell_id;
    t.price         = price;
    t.quantity      = qty;
    t.timestamp     = chrono::high_resolution_clock::now().time_since_epoch();
    return t;
}

} // namespace lob
