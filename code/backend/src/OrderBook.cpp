#include "lob/OrderBook.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

using namespace std;

namespace lob {

OrderBook::OrderBook(string symbol)
    : symbol_(move(symbol)) {}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

bool OrderBook::add_order(Order order) {
    if (orders_.count(order.id)) return false;

    const uint64_t resting_qty = order.remaining();
    const double   price       = order.price;
    const Side     side        = order.side;

    if (side == Side::BUY) {
        auto [it, _] = bids_.emplace(price, PriceLevel{price});
        it->second.total_quantity += resting_qty;
        it->second.order_ids.push_back(order.id);
    } else {
        auto [it, _] = asks_.emplace(price, PriceLevel{price});
        it->second.total_quantity += resting_qty;
        it->second.order_ids.push_back(order.id);
    }

    orders_.emplace(order.id, move(order));
    return true;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) return false;
    if (!it->second.is_active()) return false;

    Order& o = it->second;
    o.status = OrderStatus::CANCELLED;
    remove_from_level_internal(o.side, o.price, order_id, o.remaining());
    orders_.erase(it);
    return true;
}

bool OrderBook::modify_order(uint64_t order_id, uint64_t new_quantity) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) return false;
    if (!it->second.is_active()) return false;

    Order& o = it->second;
    if (new_quantity <= o.filled_quantity) return false;

    const uint64_t old_remaining = o.remaining();
    const uint64_t new_remaining = new_quantity - o.filled_quantity;

    // Update the level's aggregate quantity.
    if (o.side == Side::BUY) {
        auto lvl = bids_.find(o.price);
        if (lvl != bids_.end())
            lvl->second.total_quantity =
                lvl->second.total_quantity - old_remaining + new_remaining;
    } else {
        auto lvl = asks_.find(o.price);
        if (lvl != asks_.end())
            lvl->second.total_quantity =
                lvl->second.total_quantity - old_remaining + new_remaining;
    }

    o.quantity = new_quantity;
    // NOTE: For a quantity increase, most exchanges treat this as a
    // cancel-replace (losing time priority).  For a quantity decrease the
    // order keeps its place.  We preserve priority in both cases because most
    // research simulators use this simpler model.  Callers requiring strict
    // exchange semantics should cancel + resubmit for increases.
    return true;
}

void OrderBook::reset() noexcept {
    bids_.clear();
    asks_.clear();
    orders_.clear();
}

// ---------------------------------------------------------------------------
// Engine-internal helpers
// ---------------------------------------------------------------------------

void OrderBook::apply_fill(uint64_t order_id, uint64_t fill_qty) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) return;

    Order& o = it->second;
    fill_qty = min(fill_qty, o.remaining());

    o.filled_quantity += fill_qty;
    o.status = (o.remaining() == 0) ? OrderStatus::FILLED : OrderStatus::PARTIAL;

    // Deduct from the level's running total.  order_ids list management is
    // handled by the caller (MatchingEngine::cross()).
    if (o.side == Side::BUY) {
        auto lvl = bids_.find(o.price);
        if (lvl != bids_.end() && lvl->second.total_quantity >= fill_qty)
            lvl->second.total_quantity -= fill_qty;
        else if (lvl != bids_.end())
            lvl->second.total_quantity = 0; // guard against underflow
    } else {
        auto lvl = asks_.find(o.price);
        if (lvl != asks_.end() && lvl->second.total_quantity >= fill_qty)
            lvl->second.total_quantity -= fill_qty;
        else if (lvl != asks_.end())
            lvl->second.total_quantity = 0;
    }
}

void OrderBook::remove_fully_filled(uint64_t order_id) {
    auto oit = orders_.find(order_id);
    if (oit == orders_.end()) return;

    const Order& o = oit->second;
    if (o.side == Side::BUY) {
        auto lvl = bids_.find(o.price);
        if (lvl != bids_.end()) {
            lvl->second.order_ids.remove(order_id);
            if (lvl->second.order_ids.empty())
                bids_.erase(lvl);
        }
    } else {
        auto lvl = asks_.find(o.price);
        if (lvl != asks_.end()) {
            lvl->second.order_ids.remove(order_id);
            if (lvl->second.order_ids.empty())
                asks_.erase(lvl);
        }
    }
    orders_.erase(oit);
}

// ---------------------------------------------------------------------------
// Best prices / simple statistics
// ---------------------------------------------------------------------------

optional<double> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) return nullopt;
    return bids_.begin()->first;
}

optional<double> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return nullopt;
    return asks_.begin()->first;
}

optional<double> OrderBook::mid_price() const noexcept {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) return nullopt;
    return (*bid + *ask) * 0.5;
}

optional<double> OrderBook::spread() const noexcept {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) return nullopt;
    return *ask - *bid;
}

optional<double> OrderBook::relative_spread() const noexcept {
    auto mid = mid_price();
    auto spd = spread();
    if (!mid || !spd || *mid == 0.0) return nullopt;
    return *spd / *mid;
}

uint64_t OrderBook::bid_depth() const noexcept {
    uint64_t total = 0;
    for (auto& [p, lvl] : bids_) total += lvl.total_quantity;
    return total;
}

uint64_t OrderBook::ask_depth() const noexcept {
    uint64_t total = 0;
    for (auto& [p, lvl] : asks_) total += lvl.total_quantity;
    return total;
}

size_t OrderBook::order_count() const noexcept {
    return orders_.size();
}

size_t OrderBook::level_count_bids() const noexcept {
    return bids_.size();
}

size_t OrderBook::level_count_asks() const noexcept {
    return asks_.size();
}

double OrderBook::imbalance() const noexcept {
    const uint64_t b = bid_depth();
    const uint64_t a = ask_depth();
    const uint64_t total = b + a;
    if (total == 0) return 0.0;
    return (static_cast<double>(b) - static_cast<double>(a)) /
            static_cast<double>(total);
}

VWAPResult OrderBook::bid_vwap(size_t levels) const noexcept {
    VWAPResult result{0.0, 0, false};
    if (bids_.empty()) return result;

    double   weighted_sum = 0.0;
    uint64_t total_qty    = 0;
    size_t n = 0;

    for (auto& [p, lvl] : bids_) {
        if (n++ >= levels) break;
        weighted_sum += p * static_cast<double>(lvl.total_quantity);
        total_qty    += lvl.total_quantity;
    }

    result.total_qty = total_qty;
    result.valid     = (total_qty > 0);
    result.vwap      = (total_qty > 0)
                       ? weighted_sum / static_cast<double>(total_qty)
                       : 0.0;
    return result;
}

VWAPResult OrderBook::ask_vwap(size_t levels) const noexcept {
    VWAPResult result{0.0, 0, false};
    if (asks_.empty()) return result;

    double   weighted_sum = 0.0;
    uint64_t total_qty    = 0;
    size_t n = 0;

    for (auto& [p, lvl] : asks_) {
        if (n++ >= levels) break;
        weighted_sum += p * static_cast<double>(lvl.total_quantity);
        total_qty    += lvl.total_quantity;
    }

    result.total_qty = total_qty;
    result.valid     = (total_qty > 0);
    result.vwap      = (total_qty > 0)
                       ? weighted_sum / static_cast<double>(total_qty)
                       : 0.0;
    return result;
}

uint64_t OrderBook::available_qty_at_price(Side side, double price) const noexcept {
    uint64_t qty = 0;
    if (side == Side::BUY) {
        // Available to sell into: bids >= price
        for (auto& [p, lvl] : bids_) {
            if (p < price) break; // bids are descending
            qty += lvl.total_quantity;
        }
    } else {
        // Available to buy from: asks <= price
        for (auto& [p, lvl] : asks_) {
            if (p > price) break; // asks are ascending
            qty += lvl.total_quantity;
        }
    }
    return qty;
}

optional<double> OrderBook::estimate_market_impact(
    Side side, uint64_t qty) const noexcept {

    if (qty == 0) return nullopt;

    double   weighted_sum = 0.0;
    uint64_t remaining    = qty;

    if (side == Side::BUY) {
        // Walk asks in ascending price order
        for (auto& [p, lvl] : asks_) {
            if (remaining == 0) break;
            uint64_t fill = min(remaining, lvl.total_quantity);
            weighted_sum += p * static_cast<double>(fill);
            remaining    -= fill;
        }
    } else {
        // Walk bids in descending price order
        for (auto& [p, lvl] : bids_) {
            if (remaining == 0) break;
            uint64_t fill = min(remaining, lvl.total_quantity);
            weighted_sum += p * static_cast<double>(fill);
            remaining    -= fill;
        }
    }

    if (remaining > 0) return nullopt; // insufficient liquidity

    return weighted_sum / static_cast<double>(qty);
}

BookSnapshot OrderBook::snapshot(size_t levels) const {
    BookSnapshot snap;
    snap.symbol    = symbol_;
    snap.timestamp = chrono::high_resolution_clock::now().time_since_epoch();

    size_t n = 0;
    for (auto& [p, lvl] : bids_) {
        if (n++ >= levels) break;
        snap.bids.emplace_back(p, lvl.total_quantity);
    }

    n = 0;
    for (auto& [p, lvl] : asks_) {
        if (n++ >= levels) break;
        snap.asks.emplace_back(p, lvl.total_quantity);
    }

    return snap;
}

const Order* OrderBook::find_order(uint64_t order_id) const noexcept {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) return nullptr;
    return &it->second;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void OrderBook::remove_from_level_internal(Side     side,
                                            double   price,
                                            uint64_t order_id,
                                            uint64_t qty) noexcept {
    if (side == Side::BUY) {
        auto lvl = bids_.find(price);
        if (lvl == bids_.end()) return;
        if (lvl->second.total_quantity >= qty)
            lvl->second.total_quantity -= qty;
        else
            lvl->second.total_quantity = 0; // guard against underflow
        lvl->second.order_ids.remove(order_id);
        if (lvl->second.order_ids.empty()) bids_.erase(lvl);
    } else {
        auto lvl = asks_.find(price);
        if (lvl == asks_.end()) return;
        if (lvl->second.total_quantity >= qty)
            lvl->second.total_quantity -= qty;
        else
            lvl->second.total_quantity = 0;
        lvl->second.order_ids.remove(order_id);
        if (lvl->second.order_ids.empty()) asks_.erase(lvl);
    }
}

} // namespace lob
