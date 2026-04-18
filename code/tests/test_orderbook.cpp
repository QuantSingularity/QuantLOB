// Catch2 v3: no CATCH_CONFIG_MAIN needed — link Catch2::Catch2WithMain
#include <catch2/catch_all.hpp>

#include "lob/FeedHandler.hpp"
#include "lob/Latency.hpp"
#include "lob/MatchingEngine.hpp"
#include "lob/MemoryPool.hpp"
#include "lob/Order.hpp"
#include "lob/OrderBook.hpp"
#include "lob/RingBuffer.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace lob;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static Order make_order(uint64_t    id,
                        Side        side,
                        double      price,
                        uint64_t    qty,
                        OrderType   type = OrderType::LIMIT,
                        std::string sym  = "AAPL") {
    return Order{id, side, type, price, qty, std::move(sym),
                 std::chrono::nanoseconds{0}};
}

// ===========================================================================
// Order helpers
// ===========================================================================

TEST_CASE("Order: remaining() and fill_ratio()", "[order]") {
    Order o = make_order(1, Side::BUY, 100.0, 200);
    REQUIRE(o.remaining()   == 200);
    REQUIRE(o.fill_ratio()  == Catch::Approx(0.0));

    o.filled_quantity = 80;
    o.status = OrderStatus::PARTIAL;
    REQUIRE(o.remaining()   == 120);
    REQUIRE(o.fill_ratio()  == Catch::Approx(0.4));
}

TEST_CASE("Order: is_active() states", "[order]") {
    Order o = make_order(1, Side::BUY, 100.0, 10);
    REQUIRE(o.is_active());
    o.status = OrderStatus::PARTIAL;
    REQUIRE(o.is_active());
    o.status = OrderStatus::FILLED;
    REQUIRE_FALSE(o.is_active());
    o.status = OrderStatus::CANCELLED;
    REQUIRE_FALSE(o.is_active());
}

TEST_CASE("Order: to_string helpers", "[order]") {
    REQUIRE(std::string(to_string(Side::BUY))          == "BUY");
    REQUIRE(std::string(to_string(Side::SELL))         == "SELL");
    REQUIRE(std::string(to_string(OrderType::LIMIT))   == "LIMIT");
    REQUIRE(std::string(to_string(OrderType::MARKET))  == "MARKET");
    REQUIRE(std::string(to_string(OrderType::IOC))     == "IOC");
    REQUIRE(std::string(to_string(OrderType::FOK))     == "FOK");
    REQUIRE(std::string(to_string(OrderStatus::ACTIVE)) == "ACTIVE");
    REQUIRE(std::string(to_string(OrderStatus::FILLED)) == "FILLED");
}

// ===========================================================================
// OrderBook — basic operations
// ===========================================================================

TEST_CASE("OrderBook: add and retrieve orders", "[orderbook]") {
    OrderBook book{"AAPL"};
    REQUIRE(book.add_order(make_order(1, Side::BUY,   99.0, 100)));
    REQUIRE(book.add_order(make_order(2, Side::BUY,   98.0, 200)));
    REQUIRE(book.add_order(make_order(3, Side::SELL, 101.0, 150)));
    REQUIRE(book.add_order(make_order(4, Side::SELL, 102.0,  50)));
    REQUIRE(book.order_count() == 4);
    REQUIRE(book.best_bid().value() == Catch::Approx(99.0));
    REQUIRE(book.best_ask().value() == Catch::Approx(101.0));
    REQUIRE(book.mid_price().value() == Catch::Approx(100.0));
    REQUIRE(book.spread().value()    == Catch::Approx(2.0));
}

TEST_CASE("OrderBook: duplicate order id rejected", "[orderbook]") {
    OrderBook book{"AAPL"};
    REQUIRE(book.add_order(make_order(1, Side::BUY, 100.0, 50)));
    REQUIRE_FALSE(book.add_order(make_order(1, Side::BUY, 100.0, 50)));
}

TEST_CASE("OrderBook: cancel order removes it", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 99.0, 100));
    book.add_order(make_order(2, Side::BUY, 98.0, 200));
    REQUIRE(book.cancel_order(1));
    REQUIRE(book.best_bid().value() == Catch::Approx(98.0));
    REQUIRE(book.order_count() == 1);
}

TEST_CASE("OrderBook: cancel nonexistent order returns false", "[orderbook]") {
    OrderBook book{"AAPL"};
    REQUIRE_FALSE(book.cancel_order(999));
}

TEST_CASE("OrderBook: empty book has no best bid/ask", "[orderbook]") {
    OrderBook book{"AAPL"};
    REQUIRE_FALSE(book.best_bid().has_value());
    REQUIRE_FALSE(book.best_ask().has_value());
    REQUIRE_FALSE(book.mid_price().has_value());
    REQUIRE_FALSE(book.spread().has_value());
}

TEST_CASE("OrderBook: modify order quantity upward", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 100.0, 100));
    REQUIRE(book.modify_order(1, 200));
    REQUIRE(book.bid_depth() == 200);
}

TEST_CASE("OrderBook: modify order quantity downward", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 100.0, 100));
    REQUIRE(book.modify_order(1, 50));
    REQUIRE(book.bid_depth() == 50);
}

TEST_CASE("OrderBook: modify to zero or below filled quantity fails", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 100.0, 100));
    REQUIRE_FALSE(book.modify_order(1, 0));
}

TEST_CASE("OrderBook: snapshot returns correct levels", "[orderbook]") {
    OrderBook book{"AAPL"};
    for (uint64_t i = 1; i <= 10; ++i)
        book.add_order(make_order(i, Side::BUY,
                                  100.0 - static_cast<double>(i), 10 * i));
    auto snap = book.snapshot(5);
    REQUIRE(snap.bids.size() == 5);
    REQUIRE(snap.bids[0].first == Catch::Approx(99.0));
    REQUIRE(snap.bids[4].first == Catch::Approx(95.0));
}

TEST_CASE("OrderBook: snapshot returns all levels when fewer than requested", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 99.0, 10));
    book.add_order(make_order(2, Side::BUY, 98.0, 10));
    auto snap = book.snapshot(10); // request 10 but only 2 levels exist
    REQUIRE(snap.bids.size() == 2);
}

TEST_CASE("OrderBook: bid and ask depth accumulation", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY,   99.0, 100));
    book.add_order(make_order(2, Side::BUY,   98.0, 200));
    book.add_order(make_order(3, Side::SELL, 101.0,  50));
    REQUIRE(book.bid_depth() == 300);
    REQUIRE(book.ask_depth() ==  50);
}

TEST_CASE("OrderBook: cancel cleans up price level", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 99.0, 100));
    book.cancel_order(1);
    REQUIRE_FALSE(book.best_bid().has_value());
    REQUIRE(book.order_count() == 0);
    REQUIRE(book.bid_depth() == 0);
}

TEST_CASE("OrderBook: reset clears all state", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY,   99.0, 100));
    book.add_order(make_order(2, Side::SELL, 101.0,  50));
    book.reset();
    REQUIRE(book.order_count() == 0);
    REQUIRE(book.bid_depth()   == 0);
    REQUIRE(book.ask_depth()   == 0);
    REQUIRE_FALSE(book.best_bid().has_value());
    REQUIRE_FALSE(book.best_ask().has_value());
}

TEST_CASE("OrderBook: find_order returns correct pointer", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(7, Side::BUY, 99.0, 100));
    const Order* o = book.find_order(7);
    REQUIRE(o != nullptr);
    REQUIRE(o->id == 7);
    REQUIRE(book.find_order(999) == nullptr);
}

TEST_CASE("OrderBook: level_count tracks correctly", "[orderbook]") {
    OrderBook book{"AAPL"};
    REQUIRE(book.level_count_bids() == 0);
    REQUIRE(book.level_count_asks() == 0);
    book.add_order(make_order(1, Side::BUY,   99.0, 10));
    book.add_order(make_order(2, Side::BUY,   98.0, 10));
    book.add_order(make_order(3, Side::SELL, 101.0, 10));
    REQUIRE(book.level_count_bids() == 2);
    REQUIRE(book.level_count_asks() == 1);
    book.cancel_order(1);
    REQUIRE(book.level_count_bids() == 1);
}

// ===========================================================================
// OrderBook — analytics
// ===========================================================================

TEST_CASE("OrderBook: relative_spread", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY,   99.0, 100));
    book.add_order(make_order(2, Side::SELL, 101.0, 100));
    // spread = 2, mid = 100, rel_spread = 2/100 = 0.02
    REQUIRE(book.relative_spread().has_value());
    REQUIRE(book.relative_spread().value() == Catch::Approx(0.02));
}

TEST_CASE("OrderBook: relative_spread nullopt when empty", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    REQUIRE_FALSE(book.relative_spread().has_value());
}

TEST_CASE("OrderBook: imbalance balanced book", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY,  99.0, 100));
    book.add_order(make_order(2, Side::SELL, 101.0, 100));
    REQUIRE(book.imbalance() == Catch::Approx(0.0));
}

TEST_CASE("OrderBook: imbalance bid-heavy", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY,   99.0, 300));
    book.add_order(make_order(2, Side::SELL, 101.0, 100));
    // imbalance = (300-100)/(300+100) = 0.5
    REQUIRE(book.imbalance() == Catch::Approx(0.5));
}

TEST_CASE("OrderBook: imbalance empty book returns zero", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    REQUIRE(book.imbalance() == Catch::Approx(0.0));
}

TEST_CASE("OrderBook: bid_vwap single level", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 99.0, 200));
    auto r = book.bid_vwap(5);
    REQUIRE(r.valid);
    REQUIRE(r.vwap == Catch::Approx(99.0));
    REQUIRE(r.total_qty == 200);
}

TEST_CASE("OrderBook: bid_vwap two levels", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 100.0, 100));
    book.add_order(make_order(2, Side::BUY,  99.0, 100));
    auto r = book.bid_vwap(5);
    REQUIRE(r.valid);
    // VWAP = (100*100 + 99*100) / 200 = 99.5
    REQUIRE(r.vwap == Catch::Approx(99.5));
    REQUIRE(r.total_qty == 200);
}

TEST_CASE("OrderBook: ask_vwap empty side not valid", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    auto r = book.ask_vwap(5);
    REQUIRE_FALSE(r.valid);
    REQUIRE(r.total_qty == 0);
}

TEST_CASE("OrderBook: available_qty_at_price bid side", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 100.0, 50));
    book.add_order(make_order(2, Side::BUY,  99.0, 100));
    book.add_order(make_order(3, Side::BUY,  98.0, 200));
    // available at price >= 99.0 on bid side
    REQUIRE(book.available_qty_at_price(Side::BUY, 99.0) == 150);
}

TEST_CASE("OrderBook: estimate_market_impact BUY", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::SELL, 100.0, 100));
    book.add_order(make_order(2, Side::SELL, 101.0, 100));
    // Buy 150 units: 100@100 + 50@101 => VWAP = (10000+5050)/150 ≈ 100.333...
    auto r = book.estimate_market_impact(Side::BUY, 150);
    REQUIRE(r.has_value());
    REQUIRE(r.value() == Catch::Approx(100.0 + 50.0 * 1.0 / 150.0).epsilon(0.001));
}

TEST_CASE("OrderBook: estimate_market_impact insufficient liquidity", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::SELL, 100.0, 50));
    auto r = book.estimate_market_impact(Side::BUY, 200);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("OrderBook: estimate_market_impact zero qty nullopt", "[orderbook][analytics]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::SELL, 100.0, 100));
    REQUIRE_FALSE(book.estimate_market_impact(Side::BUY, 0).has_value());
}

// ===========================================================================
// MatchingEngine — core matching
// ===========================================================================

TEST_CASE("MatchingEngine: limit order rests on book", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    auto result = engine.submit_order(make_order(1, Side::BUY, 99.0, 100));
    REQUIRE(result.resting);
    REQUIRE(result.trades.empty());
    REQUIRE_FALSE(result.fully_filled);
}

TEST_CASE("MatchingEngine: crossing limit orders match with correct IDs", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 100));
    auto result = engine.submit_order(make_order(2, Side::BUY, 101.0, 100));
    REQUIRE_FALSE(result.trades.empty());
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades[0].buy_order_id  == 2);
    REQUIRE(result.trades[0].sell_order_id == 1);
    REQUIRE(result.trades[0].quantity == 100);
    REQUIRE(result.trades[0].price == Catch::Approx(100.0)); // passive price
}

TEST_CASE("MatchingEngine: passive order removed after full fill", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 50));
    engine.submit_order(make_order(2, Side::BUY,  100.0, 50));
    auto* book = engine.get_book("AAPL");
    REQUIRE(book->order_count() == 0);
    REQUIRE_FALSE(book->best_ask().has_value());
}

TEST_CASE("MatchingEngine: partial fill leaves remainder on book", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 200));
    auto result = engine.submit_order(make_order(2, Side::BUY, 100.0, 80));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades[0].quantity == 80);
    auto* book = engine.get_book("AAPL");
    REQUIRE(book->ask_depth() == 120);
    REQUIRE(book->order_count() == 1);
}

TEST_CASE("MatchingEngine: multi-level sweep", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 50));
    engine.submit_order(make_order(2, Side::SELL, 101.0, 50));
    engine.submit_order(make_order(3, Side::SELL, 102.0, 50));
    auto result = engine.submit_order(make_order(4, Side::BUY, 103.0, 150));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades.size() == 3);
    REQUIRE(result.trades[0].price == Catch::Approx(100.0));
    REQUIRE(result.trades[1].price == Catch::Approx(101.0));
    REQUIRE(result.trades[2].price == Catch::Approx(102.0));
    REQUIRE(engine.get_book("AAPL")->order_count() == 0);
}

TEST_CASE("MatchingEngine: non-crossing limit orders both rest", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    auto r1 = engine.submit_order(make_order(1, Side::BUY,   99.0, 100));
    auto r2 = engine.submit_order(make_order(2, Side::SELL, 101.0, 100));
    REQUIRE(r1.resting);
    REQUIRE(r2.resting);
    REQUIRE(r1.trades.empty());
    REQUIRE(r2.trades.empty());
}

TEST_CASE("MatchingEngine: trade callback fires", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    int trade_count = 0;
    engine.set_trade_callback([&](const Trade&) { ++trade_count; });
    engine.submit_order(make_order(1, Side::SELL, 100.0, 50));
    engine.submit_order(make_order(2, Side::BUY,  100.0, 50));
    REQUIRE(trade_count >= 1);
}

TEST_CASE("MatchingEngine: fill callback fires for both sides", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    std::vector<uint64_t> filled_ids;
    engine.set_fill_callback([&](uint64_t id, uint64_t, double) {
        filled_ids.push_back(id);
    });
    engine.submit_order(make_order(1, Side::SELL, 100.0, 50));
    engine.submit_order(make_order(2, Side::BUY,  100.0, 50));
    // Both buy_order_id (2) and sell_order_id (1) should appear
    REQUIRE(filled_ids.size() == 2);
    bool has_passive  = std::find(filled_ids.begin(), filled_ids.end(), 1) != filled_ids.end();
    bool has_aggressor= std::find(filled_ids.begin(), filled_ids.end(), 2) != filled_ids.end();
    REQUIRE(has_passive);
    REQUIRE(has_aggressor);
}

TEST_CASE("MatchingEngine: cancel order via engine", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::BUY, 99.0, 100));
    REQUIRE(engine.cancel_order("AAPL", 1));
    REQUIRE(engine.stats().orders_cancelled == 1);
}

TEST_CASE("MatchingEngine: cancel nonexistent order returns false", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    REQUIRE_FALSE(engine.cancel_order("AAPL", 9999));
}

TEST_CASE("MatchingEngine: cancel on unregistered symbol returns false", "[matching]") {
    MatchingEngine engine;
    REQUIRE_FALSE(engine.cancel_order("ZZZZ", 1));
}

TEST_CASE("MatchingEngine: market order against empty book", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    auto o = make_order(1, Side::BUY, 0.0, 100, OrderType::MARKET);
    auto result = engine.submit_order(std::move(o));
    REQUIRE(result.trades.empty());
    REQUIRE_FALSE(result.fully_filled);
    REQUIRE_FALSE(result.resting);
}

TEST_CASE("MatchingEngine: market order fills from book", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 200));
    auto result = engine.submit_order(make_order(2, Side::BUY, 0.0, 100, OrderType::MARKET));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades.size() == 1);
    REQUIRE(result.trades[0].quantity == 100);
}

TEST_CASE("MatchingEngine: market order sweeps multiple levels", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 30));
    engine.submit_order(make_order(2, Side::SELL, 101.0, 30));
    engine.submit_order(make_order(3, Side::SELL, 102.0, 30));
    auto result = engine.submit_order(make_order(4, Side::BUY, 0.0, 90, OrderType::MARKET));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades.size() == 3);
}

TEST_CASE("MatchingEngine: IOC cancels unfilled remainder", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 30));
    auto result = engine.submit_order(make_order(2, Side::BUY, 100.0, 100, OrderType::IOC));
    REQUIRE(result.trades.size() == 1);
    REQUIRE(result.trades[0].quantity == 30);
    REQUIRE_FALSE(result.resting);
    REQUIRE_FALSE(engine.get_book("AAPL")->best_bid().has_value());
}

TEST_CASE("MatchingEngine: IOC fully filled when liquidity sufficient", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 100));
    auto result = engine.submit_order(make_order(2, Side::BUY, 100.0, 100, OrderType::IOC));
    REQUIRE(result.fully_filled);
    REQUIRE_FALSE(result.resting);
}

TEST_CASE("MatchingEngine: FOK rejected when insufficient liquidity", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 50));
    auto result = engine.submit_order(make_order(2, Side::BUY, 100.0, 200, OrderType::FOK));
    REQUIRE(result.rejected);
    REQUIRE(result.trades.empty());
    REQUIRE(engine.stats().orders_rejected == 1);
    REQUIRE(engine.get_book("AAPL")->ask_depth() == 50);
}

TEST_CASE("MatchingEngine: FOK fills when sufficient liquidity", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 300));
    auto result = engine.submit_order(make_order(2, Side::BUY, 100.0, 200, OrderType::FOK));
    REQUIRE_FALSE(result.rejected);
    REQUIRE(result.fully_filled);
    REQUIRE(engine.get_book("AAPL")->ask_depth() == 100);
}

TEST_CASE("MatchingEngine: FOK rejected leaves book intact", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 10));
    engine.submit_order(make_order(2, Side::SELL, 101.0, 10));
    auto result = engine.submit_order(make_order(3, Side::BUY, 102.0, 30, OrderType::FOK));
    REQUIRE(result.rejected);
    REQUIRE(engine.get_book("AAPL")->ask_depth() == 20);
    REQUIRE(engine.get_book("AAPL")->order_count() == 2);
}

TEST_CASE("MatchingEngine: reject callback fires on FOK rejection", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    bool rejected_cb_fired = false;
    engine.set_reject_callback([&](uint64_t, const std::string&) {
        rejected_cb_fired = true;
    });
    engine.submit_order(make_order(1, Side::SELL, 100.0, 10));
    engine.submit_order(make_order(2, Side::BUY, 100.0, 100, OrderType::FOK));
    REQUIRE(rejected_cb_fired);
}

TEST_CASE("MatchingEngine: stats accumulate correctly", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    for (uint64_t i = 1; i <= 5; ++i)
        engine.submit_order(make_order(i, Side::BUY,
                                       100.0 - static_cast<double>(i), 10));
    REQUIRE(engine.stats().orders_processed == 5);
    REQUIRE(engine.stats().orders_resting   == 5);
}

TEST_CASE("MatchingEngine: total_notional accumulates", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 100));
    engine.submit_order(make_order(2, Side::BUY,  100.0, 100));
    REQUIRE(engine.stats().total_notional == Catch::Approx(10000.0));
    REQUIRE(engine.stats().total_volume == 100);
}

TEST_CASE("MatchingEngine: auto-registers unknown symbol", "[matching]") {
    MatchingEngine engine;
    auto result = engine.submit_order(
        make_order(1, Side::BUY, 50.0, 10, OrderType::LIMIT, "TSLA"));
    REQUIRE(result.resting);
    REQUIRE(engine.get_book("TSLA") != nullptr);
}

TEST_CASE("MatchingEngine: has_symbol after registration", "[matching]") {
    MatchingEngine engine;
    REQUIRE_FALSE(engine.has_symbol("AAPL"));
    engine.register_symbol("AAPL");
    REQUIRE(engine.has_symbol("AAPL"));
}

TEST_CASE("MatchingEngine: price-time priority FIFO within level", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(10, Side::SELL, 100.0, 50));
    engine.submit_order(make_order(11, Side::SELL, 100.0, 50));
    auto result = engine.submit_order(make_order(20, Side::BUY, 100.0, 50));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades[0].sell_order_id == 10);
}

TEST_CASE("MatchingEngine: reset_book clears state", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::BUY,  99.0, 100));
    engine.submit_order(make_order(2, Side::SELL, 101.0, 50));
    engine.reset_book("AAPL");
    auto* book = engine.get_book("AAPL");
    REQUIRE(book != nullptr);
    REQUIRE(book->order_count() == 0);
    REQUIRE_FALSE(book->best_bid().has_value());
    REQUIRE_FALSE(book->best_ask().has_value());
}

TEST_CASE("MatchingEngine: reset_all_books clears every symbol", "[matching]") {
    MatchingEngine engine;
    engine.submit_order(make_order(1, Side::BUY, 100.0, 10, OrderType::LIMIT, "AAPL"));
    engine.submit_order(make_order(2, Side::BUY, 200.0, 10, OrderType::LIMIT, "TSLA"));
    engine.reset_all_books();
    REQUIRE(engine.get_book("AAPL")->order_count() == 0);
    REQUIRE(engine.get_book("TSLA")->order_count() == 0);
}

TEST_CASE("MatchingEngine: reset_stats zeroes counters", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::BUY, 99.0, 10));
    REQUIRE(engine.stats().orders_processed == 1);
    engine.reset_stats();
    REQUIRE(engine.stats().orders_processed == 0);
    REQUIRE(engine.stats().total_notional   == Catch::Approx(0.0));
}

TEST_CASE("MatchingEngine: modify order via engine", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::BUY, 99.0, 100));
    REQUIRE(engine.modify_order("AAPL", 1, 200));
    REQUIRE(engine.get_book("AAPL")->bid_depth() == 200);
}

TEST_CASE("MatchingEngine: modify on unregistered symbol returns false", "[matching]") {
    MatchingEngine engine;
    REQUIRE_FALSE(engine.modify_order("ZZZZ", 1, 100));
}

TEST_CASE("MatchingEngine: sell aggressor crosses buy side", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::BUY, 100.0, 100));
    auto result = engine.submit_order(make_order(2, Side::SELL, 99.0, 100));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades[0].price == Catch::Approx(100.0));
    REQUIRE(result.trades[0].buy_order_id  == 1);
    REQUIRE(result.trades[0].sell_order_id == 2);
}

TEST_CASE("MatchingEngine: aggressor partial fill rests remainder", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::SELL, 100.0, 30));
    auto result = engine.submit_order(make_order(2, Side::BUY, 100.0, 100));
    REQUIRE_FALSE(result.fully_filled);
    REQUIRE(result.resting);
    REQUIRE(result.trades[0].quantity == 30);
    REQUIRE(engine.get_book("AAPL")->bid_depth() == 70);
}

TEST_CASE("MatchingEngine: multiple orders at same price maintain FIFO for partial fill", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    // Three sells at the same price
    engine.submit_order(make_order(1, Side::SELL, 100.0, 30));
    engine.submit_order(make_order(2, Side::SELL, 100.0, 30));
    engine.submit_order(make_order(3, Side::SELL, 100.0, 30));
    // Buy 50: should consume all of order 1 (30) and part of order 2 (20)
    auto result = engine.submit_order(make_order(4, Side::BUY, 100.0, 50));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades.size() == 2);
    REQUIRE(result.trades[0].sell_order_id == 1);
    REQUIRE(result.trades[0].quantity == 30);
    REQUIRE(result.trades[1].sell_order_id == 2);
    REQUIRE(result.trades[1].quantity == 20);
    // Order 2 should have 10 remaining, order 3 untouched
    REQUIRE(engine.get_book("AAPL")->ask_depth() == 40);
}

TEST_CASE("MatchingEngine: sell market sweeps bids descending", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::BUY, 102.0, 50));
    engine.submit_order(make_order(2, Side::BUY, 101.0, 50));
    engine.submit_order(make_order(3, Side::BUY, 100.0, 50));
    auto result = engine.submit_order(make_order(4, Side::SELL, 0.0, 150, OrderType::MARKET));
    REQUIRE(result.fully_filled);
    REQUIRE(result.trades.size() == 3);
    // First fill should be against the best (highest) bid
    REQUIRE(result.trades[0].price == Catch::Approx(102.0));
    REQUIRE(result.trades[1].price == Catch::Approx(101.0));
    REQUIRE(result.trades[2].price == Catch::Approx(100.0));
}

TEST_CASE("MatchingEngine: FOK sell side rejected preserves bids", "[matching]") {
    MatchingEngine engine;
    engine.register_symbol("AAPL");
    engine.submit_order(make_order(1, Side::BUY, 100.0, 10));
    auto result = engine.submit_order(make_order(2, Side::SELL, 99.0, 50, OrderType::FOK));
    REQUIRE(result.rejected);
    REQUIRE(engine.get_book("AAPL")->bid_depth() == 10);
}

// ===========================================================================
// RingBuffer
// ===========================================================================

TEST_CASE("RingBuffer: push and pop basic", "[ringbuffer]") {
    RingBuffer<int, 8> rb;
    REQUIRE(rb.empty());
    REQUIRE(rb.push(42));
    REQUIRE(rb.size() == 1);
    auto v = rb.pop();
    REQUIRE(v.has_value());
    REQUIRE(*v == 42);
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer: full buffer rejects push", "[ringbuffer]") {
    RingBuffer<int, 4> rb;
    REQUIRE(rb.push(1));
    REQUIRE(rb.push(2));
    REQUIRE(rb.push(3));
    REQUIRE_FALSE(rb.push(4)); // capacity=4, usable=3 (one sentinel slot)
}

TEST_CASE("RingBuffer: pop on empty returns nullopt", "[ringbuffer]") {
    RingBuffer<int, 8> rb;
    REQUIRE_FALSE(rb.pop().has_value());
}

TEST_CASE("RingBuffer: FIFO ordering preserved", "[ringbuffer]") {
    RingBuffer<int, 16> rb;
    for (int i = 0; i < 8; ++i) rb.push(i);
    for (int i = 0; i < 8; ++i) {
        auto v = rb.pop();
        REQUIRE(v.has_value());
        REQUIRE(*v == i);
    }
}

TEST_CASE("RingBuffer: size tracks correctly", "[ringbuffer]") {
    RingBuffer<int, 16> rb;
    REQUIRE(rb.size() == 0);
    rb.push(1); rb.push(2); rb.push(3);
    REQUIRE(rb.size() == 3);
    rb.pop();
    REQUIRE(rb.size() == 2);
}

TEST_CASE("RingBuffer: capacity is compile-time constant", "[ringbuffer]") {
    RingBuffer<int, 64> rb;
    REQUIRE(rb.capacity() == 64);
}

TEST_CASE("RingBuffer: wrap-around correctness", "[ringbuffer]") {
    RingBuffer<int, 8> rb;
    // Fill and drain twice to exercise the wrap-around path
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 7; ++i) REQUIRE(rb.push(i));
        for (int i = 0; i < 7; ++i) {
            auto v = rb.pop();
            REQUIRE(v.has_value());
            REQUIRE(*v == i);
        }
        REQUIRE(rb.empty());
    }
}

TEST_CASE("RingBuffer: move-push", "[ringbuffer]") {
    RingBuffer<std::string, 8> rb;
    std::string s = "hello";
    REQUIRE(rb.push(std::move(s)));
    auto v = rb.pop();
    REQUIRE(v.has_value());
    REQUIRE(*v == "hello");
}

// ===========================================================================
// MemoryPool
// ===========================================================================

TEST_CASE("MemoryPool: allocate and deallocate", "[mempool]") {
    MemoryPool<int, 16> pool;
    REQUIRE(pool.available() == 16);
    int* p = pool.allocate();
    REQUIRE(p != nullptr);
    REQUIRE(pool.available() == 15);
    pool.deallocate(p);
    REQUIRE(pool.available() == 16);
}

TEST_CASE("MemoryPool: exhaustion returns nullptr", "[mempool]") {
    MemoryPool<int, 2> pool;
    int* a = pool.allocate();
    int* b = pool.allocate();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(pool.allocate() == nullptr);
    pool.deallocate(a);
    pool.deallocate(b);
    REQUIRE(pool.available() == 2);
}

TEST_CASE("MemoryPool: construct and destroy helper", "[mempool]") {
    MemoryPool<int, 4> pool;
    int* p = pool.construct();
    REQUIRE(p != nullptr);
    *p = 42;
    REQUIRE(*p == 42);
    pool.destroy(p);
    REQUIRE(pool.available() == 4);
}

TEST_CASE("MemoryPool: capacity constant", "[mempool]") {
    MemoryPool<double, 32> pool;
    REQUIRE(pool.capacity() == 32);
}

TEST_CASE("MemoryPool: deallocate nullptr is safe", "[mempool]") {
    MemoryPool<int, 8> pool;
    REQUIRE_NOTHROW(pool.deallocate(nullptr));
    REQUIRE(pool.available() == 8);
}

TEST_CASE("MemoryPool: all slots reusable after full cycle", "[mempool]") {
    MemoryPool<int, 4> pool;
    std::vector<int*> ptrs;
    for (int i = 0; i < 4; ++i) ptrs.push_back(pool.allocate());
    REQUIRE(pool.available() == 0);
    for (auto* p : ptrs) pool.deallocate(p);
    REQUIRE(pool.available() == 4);
    // Should be able to allocate again
    int* p = pool.allocate();
    REQUIRE(p != nullptr);
    pool.deallocate(p);
}

// ===========================================================================
// LatencyRecorder
// ===========================================================================

TEST_CASE("LatencyRecorder: basic statistics correct", "[latency]") {
    LatencyRecorder rec;
    rec.record(std::chrono::nanoseconds{100});
    rec.record(std::chrono::nanoseconds{200});
    rec.record(std::chrono::nanoseconds{300});
    REQUIRE(rec.count()   == 3);
    REQUIRE(rec.mean_ns() == Catch::Approx(200.0));
    REQUIRE(rec.min_ns()  == 100);
    REQUIRE(rec.max_ns()  == 300);
    REQUIRE(rec.p50_ns()  == 200);
    REQUIRE(rec.sample(0) == 100);
}

TEST_CASE("LatencyRecorder: stddev_ns correct", "[latency]") {
    LatencyRecorder rec;
    rec.record(std::chrono::nanoseconds{100});
    rec.record(std::chrono::nanoseconds{200});
    rec.record(std::chrono::nanoseconds{300});
    // Sample stddev of [100,200,300] = sqrt(((100-200)^2+(200-200)^2+(300-200)^2)/2)
    //                                = sqrt(20000) ≈ 100
    REQUIRE(rec.stddev_ns() == Catch::Approx(100.0).epsilon(0.01));
}

TEST_CASE("LatencyRecorder: empty recorder safe", "[latency]") {
    LatencyRecorder rec;
    REQUIRE(rec.count()    == 0);
    REQUIRE(rec.mean_ns()  == Catch::Approx(0.0));
    REQUIRE(rec.stddev_ns()== Catch::Approx(0.0));
    REQUIRE(rec.min_ns()   == 0);
    REQUIRE(rec.max_ns()   == 0);
    REQUIRE(rec.p50_ns()   == 0);
}

TEST_CASE("LatencyRecorder: clear resets state", "[latency]") {
    LatencyRecorder rec;
    rec.record(std::chrono::nanoseconds{500});
    rec.clear();
    REQUIRE(rec.count() == 0);
}

TEST_CASE("LatencyRecorder: p99 with 100 samples", "[latency]") {
    LatencyRecorder rec;
    for (int i = 1; i <= 100; ++i)
        rec.record(std::chrono::nanoseconds{static_cast<int64_t>(i)});
    REQUIRE(rec.p99_ns() >= 98);
    REQUIRE(rec.p99_ns() <= 100);
}

TEST_CASE("LatencyRecorder: p90 plausible", "[latency]") {
    LatencyRecorder rec;
    for (int i = 1; i <= 100; ++i)
        rec.record(std::chrono::nanoseconds{static_cast<int64_t>(i)});
    REQUIRE(rec.p90_ns() >= 88);
    REQUIRE(rec.p90_ns() <= 92);
}

TEST_CASE("LatencyRecorder: merge combines samples", "[latency]") {
    LatencyRecorder a, b;
    a.record(std::chrono::nanoseconds{100});
    b.record(std::chrono::nanoseconds{200});
    b.record(std::chrono::nanoseconds{300});
    a.merge(b);
    REQUIRE(a.count() == 3);
    REQUIRE(a.mean_ns() == Catch::Approx(200.0));
}

TEST_CASE("ScopedTimer: records positive duration", "[latency]") {
    LatencyRecorder rec;
    {
        ScopedTimer t{rec};
        // no-op: just let it destruct
    }
    REQUIRE(rec.count() == 1);
    REQUIRE(rec.sample(0) >= 0);
}

// ===========================================================================
// FeedHandler
// ===========================================================================

TEST_CASE("FeedHandler: synthetic generation runs without error", "[feed]") {
    MatchingEngine engine;
    FeedHandler    feed{engine};
    FeedConfig cfg;
    cfg.type   = FeedType::SYNTHETIC;
    cfg.symbol = "AAPL";
    feed.configure(cfg);
    SyntheticConfig syn;
    syn.num_events = 1000;
    syn.mid_price  = 100.0;
    syn.tick_size  = 0.01;
    syn.seed       = 42;
    feed.generate_synthetic(syn);
    REQUIRE(feed.events_processed() == 1000);
    REQUIRE(engine.stats().orders_processed > 0);
}

TEST_CASE("FeedHandler: order callback fires for every new order submission", "[feed]") {
    MatchingEngine engine;
    FeedHandler    feed{engine};
    FeedConfig cfg;
    cfg.type   = FeedType::SYNTHETIC;
    cfg.symbol = "AAPL";
    feed.configure(cfg);

    int cb_count = 0;
    feed.set_order_callback([&](const Order&) { ++cb_count; });

    SyntheticConfig syn;
    syn.num_events = 200;
    syn.seed       = 99;
    syn.cancel_rate= 0.0; // no cancels so every event is an order
    feed.generate_synthetic(syn);
    REQUIRE(cb_count == 200);
}

TEST_CASE("FeedHandler: synthetic with high cancel rate still stable", "[feed]") {
    MatchingEngine engine;
    FeedHandler    feed{engine};
    FeedConfig cfg;
    cfg.type   = FeedType::SYNTHETIC;
    cfg.symbol = "TEST";
    feed.configure(cfg);
    SyntheticConfig syn;
    syn.num_events   = 500;
    syn.cancel_rate  = 0.9;
    syn.seed         = 7;
    REQUIRE_NOTHROW(feed.generate_synthetic(syn));
    REQUIRE(feed.events_processed() == 500);
}

TEST_CASE("FeedHandler: reset clears event counter", "[feed]") {
    MatchingEngine engine;
    FeedHandler    feed{engine};
    FeedConfig cfg;
    cfg.type   = FeedType::SYNTHETIC;
    cfg.symbol = "AAPL";
    feed.configure(cfg);
    SyntheticConfig syn;
    syn.num_events = 100;
    feed.generate_synthetic(syn);
    REQUIRE(feed.events_processed() == 100);
    feed.reset();
    REQUIRE(feed.events_processed() == 0);
}

TEST_CASE("FeedHandler: configure registers symbol with engine", "[feed]") {
    MatchingEngine engine;
    FeedHandler    feed{engine};
    FeedConfig cfg;
    cfg.type   = FeedType::SYNTHETIC;
    cfg.symbol = "NEWCO";
    feed.configure(cfg);
    REQUIRE(engine.has_symbol("NEWCO"));
}

TEST_CASE("FeedHandler: different seeds produce different trade counts", "[feed]") {
    auto run = [](uint32_t seed) {
        MatchingEngine engine;
        FeedHandler    feed{engine};
        FeedConfig cfg;
        cfg.type   = FeedType::SYNTHETIC;
        cfg.symbol = "AAPL";
        feed.configure(cfg);
        SyntheticConfig syn;
        syn.num_events = 500;
        syn.seed       = seed;
        feed.generate_synthetic(syn);
        return engine.stats().total_trades;
    };
    // Different seeds should generally produce different trade counts
    // (not strictly guaranteed, but overwhelmingly likely)
    uint64_t t1 = run(1);
    uint64_t t2 = run(99999);
    REQUIRE(t1 != t2);
}
