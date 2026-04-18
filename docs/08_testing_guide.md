# Testing Guide

## Table of Contents

1. [Test Suite Structure](#test-suite-structure)
2. [Running Tests](#running-tests)
3. [Test Tags](#test-tags)
4. [Test Coverage by Component](#test-coverage-by-component)
5. [Writing New Tests](#writing-new-tests)
6. [Sanitizer Runs](#sanitizer-runs)
7. [Continuous Integration](#continuous-integration)

---

## Test Suite Structure

All tests are in `code/tests/test_orderbook.cpp` and use Catch2 v3.
The binary links `Catch2::Catch2WithMain` so no manual `main()` is needed.

| File                               | Purpose                                                       |
| ---------------------------------- | ------------------------------------------------------------- |
| `code/tests/test_orderbook.cpp`    | Core LOB engine tests (70+ cases)                             |
| `code/ai_models/tests/test_ml.cpp` | ML module tests (feature extraction, model updates, pipeline) |

---

## Running Tests

### All tests

```bash
cd build && ctest --output-on-failure
```

### With parallel execution

```bash
cd build && ctest --output-on-failure --parallel 4
```

### Run a specific tag directly

```bash
./build/quantlob_tests [orderbook]
./build/quantlob_tests [matching]
./build/quantlob_tests [analytics]
./build/quantlob_tests [feed]
./build/quantlob_tests [latency]
./build/quantlob_tests [ringbuffer]
./build/quantlob_tests [mempool]
./build/quantlob_tests [order]
./build/quantlob_tests [ml]
```

### Run tests matching a name pattern

```bash
./build/quantlob_tests "FOK*"
./build/quantlob_tests "MatchingEngine*"
```

### Show all test names

```bash
./build/quantlob_tests --list-tests
```

---

## Test Tags

| Tag            | Component tested                                                                     |
| -------------- | ------------------------------------------------------------------------------------ |
| `[order]`      | Order struct helper methods, to_string overloads                                     |
| `[orderbook]`  | add_order, cancel_order, modify_order, reset, find_order                             |
| `[analytics]`  | relative_spread, imbalance, vwap, market_impact, available_qty                       |
| `[matching]`   | LIMIT, MARKET, IOC, FOK, multi-level sweeps, callbacks                               |
| `[feed]`       | FeedHandler synthetic generation, callbacks, reset                                   |
| `[latency]`    | LatencyRecorder statistics, ScopedTimer, merge                                       |
| `[ringbuffer]` | RingBuffer push/pop, wrap-around, FIFO ordering                                      |
| `[mempool]`    | MemoryPool allocate/deallocate cycles, exhaustion                                    |
| `[ml]`         | FeatureExtractor, MidPricePredictor, OrderFlowPredictor, AnomalyDetector, MLPipeline |

---

## Test Coverage by Component

### Order (tag: order)

| Test case                             | What it verifies                               |
| ------------------------------------- | ---------------------------------------------- |
| `Order: remaining() and fill_ratio()` | Correct arithmetic for partially filled orders |
| `Order: is_active() states`           | All five OrderStatus values                    |
| `Order: to_string helpers`            | All Side, OrderType, and OrderStatus values    |

### OrderBook (tag: orderbook)

| Test case                                 | What it verifies                      |
| ----------------------------------------- | ------------------------------------- |
| Add and retrieve orders                   | best_bid, best_ask, mid_price, spread |
| Duplicate order id rejected               | add_order returns false on duplicate  |
| Cancel order removes it                   | Level cleanup after cancel            |
| Cancel nonexistent order                  | Returns false without crash           |
| Empty book queries                        | All optionals return nullopt          |
| Modify quantity upward                    | bid_depth updated correctly           |
| Modify quantity downward                  | Reduction preserves order             |
| Modify to zero or below filled            | Returns false                         |
| Snapshot with fewer levels than requested | No out-of-bounds                      |
| Bid and ask depth accumulation            | Multi-level totals                    |
| Cancel cleans up price level              | Level removed when empty              |
| Reset                                     | All maps cleared                      |
| find_order                                | Correct pointer or nullptr            |
| level_count tracking                      | Increments and decrements             |

### Analytics (tag: analytics)

| Test case                                     | What it verifies                    |
| --------------------------------------------- | ----------------------------------- |
| relative_spread                               | Correct formula: spread / mid       |
| relative_spread nullopt when empty            | No division by zero                 |
| imbalance balanced                            | Returns 0.0                         |
| imbalance bid-heavy                           | Correct (300-100)/(300+100) = 0.5   |
| imbalance empty                               | Returns 0.0                         |
| bid_vwap single level                         | VWAP equals the single price        |
| bid_vwap two levels                           | Weighted average                    |
| ask_vwap empty side                           | Returns valid=false                 |
| available_qty_at_price                        | Cumulative depth at price threshold |
| estimate_market_impact BUY                    | Weighted average fill price         |
| estimate_market_impact insufficient liquidity | Returns nullopt                     |
| estimate_market_impact zero qty               | Returns nullopt                     |

### MatchingEngine (tag: matching)

| Test case                          | What it verifies                    |
| ---------------------------------- | ----------------------------------- |
| Limit order rests                  | result.resting == true              |
| Crossing limits match              | Correct buy/sell IDs, passive price |
| Passive removed after full fill    | order_count == 0                    |
| Partial fill leaves remainder      | ask_depth updated                   |
| Multi-level sweep                  | 3 trades in price order             |
| Non-crossing both rest             | No trades                           |
| Trade callback fires               | Counter incremented                 |
| Fill callback fires for both sides | Both IDs in callback                |
| Cancel via engine                  | stats.orders_cancelled incremented  |
| Cancel nonexistent                 | Returns false                       |
| Cancel unregistered symbol         | Returns false                       |
| Market order on empty book         | No trades, not resting              |
| Market order fills                 | Correct quantity                    |
| Market sweeps multiple levels      | 3 trades                            |
| IOC cancels remainder              | Nothing rests                       |
| IOC fully filled                   | fully_filled true                   |
| FOK rejected insufficient          | Book untouched                      |
| FOK fills sufficient               | ask_depth reduced                   |
| FOK rejected book intact           | Two orders still present            |
| Reject callback on FOK             | Callback called                     |
| Stats accumulate                   | 5 orders = 5 resting                |
| total_notional                     | 100 shares at $100 = 10000          |
| Auto-register unknown symbol       | get_book returns non-null           |
| has_symbol after registration      | True                                |
| Price-time priority FIFO           | First submitted sell matched first  |
| reset_book                         | Book cleared, still accessible      |
| reset_all_books                    | All symbols cleared                 |
| reset_stats                        | All counters zero                   |
| modify via engine                  | bid_depth updated                   |
| modify unregistered                | Returns false                       |
| Sell crosses buy side              | Passive bid price used              |
| Aggressor partial rests remainder  | bid_depth = 70                      |
| Multi-order FIFO partial fill      | Two trades, correct split           |
| Sell market sweeps bids descending | Prices in descending order          |
| FOK sell side preserves bids       | bid_depth unchanged after reject    |

### FeedHandler (tag: feed)

| Test case                                      | What it verifies                   |
| ---------------------------------------------- | ---------------------------------- |
| Synthetic runs without error                   | events_processed == num_events     |
| Order callback for every new order             | cb_count == 200 with cancel_rate=0 |
| High cancel rate stable                        | No crash or hang                   |
| Reset clears counter                           | events_processed == 0              |
| Configure registers symbol                     | has_symbol true                    |
| Different seeds produce different trade counts | Randomness confirmed               |

### Latency (tag: latency)

| Test case                             | What it verifies        |
| ------------------------------------- | ----------------------- |
| Basic statistics                      | mean, min, max, p50     |
| stddev_ns correct                     | Against known values    |
| Empty recorder safe                   | All queries return 0    |
| Clear resets state                    | count == 0              |
| p99 with 100 samples                  | In range [98, 100]      |
| p90 plausible                         | In range [88, 92]       |
| Merge combines samples                | count and mean correct  |
| ScopedTimer records positive duration | count == 1, sample >= 0 |

---

## Writing New Tests

Catch2 v3 uses `TEST_CASE("name", "[tags]")` with `REQUIRE`, `REQUIRE_FALSE`, and `REQUIRE_NOTHROW`.

### Minimal example

```cpp
TEST_CASE("OrderBook: my new test", "[orderbook]") {
    OrderBook book{"AAPL"};
    book.add_order(make_order(1, Side::BUY, 100.0, 50));
    REQUIRE(book.bid_depth() == 50);
}
```

### Floating point comparison

Always use `Catch::Approx` for doubles:

```cpp
REQUIRE(book.spread().value() == Catch::Approx(2.0));
REQUIRE(book.imbalance() == Catch::Approx(0.5).epsilon(0.001));
```

### Verifying callbacks

Use a capture lambda with a counter:

```cpp
int count = 0;
engine.set_trade_callback([&](const Trade&) { ++count; });
engine.submit_order(...);
REQUIRE(count == 1);
```

---

## Sanitizer Runs

For each sanitizer, build in Debug mode and run ctest:

| Sanitizer | CMake flag                 | What it finds                                  |
| --------- | -------------------------- | ---------------------------------------------- |
| ASan      | `QUANTLOB_ENABLE_ASAN=ON`  | Heap overflow, use-after-free, leaks           |
| TSan      | `QUANTLOB_ENABLE_TSAN=ON`  | Data races in multi-threaded code              |
| UBSan     | `QUANTLOB_ENABLE_UBSAN=ON` | Signed overflow, misaligned access, null deref |

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug -DQUANTLOB_ENABLE_ASAN=ON
cmake --build build_asan --parallel
cd build_asan && ctest --output-on-failure
```

---

## Continuous Integration

The `.github/workflows/ci.yml` pipeline runs on every push and pull request.

### CI matrix

| Job           | OS           | Compiler | Build type | Extra flags |
| ------------- | ------------ | -------- | ---------- | ----------- |
| gcc-release   | ubuntu-22.04 | GCC 12   | Release    | None        |
| gcc-debug     | ubuntu-22.04 | GCC 12   | Debug      | None        |
| clang-release | ubuntu-22.04 | Clang 14 | Release    | None        |
| clang-asan    | ubuntu-22.04 | Clang 14 | Debug      | ASAN=ON     |
| ubsan-check   | ubuntu-22.04 | Clang 14 | Debug      | UBSAN=ON    |

### CI steps per job

| Step                 | Description                                    |
| -------------------- | ---------------------------------------------- |
| Install dependencies | apt-get: ninja-build, cmake, compiler packages |
| Configure            | cmake with matrix-specific flags               |
| Build                | cmake --build --parallel                       |
| Test                 | ctest --output-on-failure --parallel 4         |
| Smoke test           | ./quantlob --events 5000 --log-level WARN      |
