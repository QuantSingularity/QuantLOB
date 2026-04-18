# QuantLOB API Reference

## Table of Contents

1. [Order Types](#order-types)
2. [OrderBook](#orderbook)
3. [MatchingEngine](#matchingengine)
4. [FeedHandler](#feedhandler)
5. [LatencyRecorder and ScopedTimer](#latencyrecorder-and-scopedtimer)
6. [Logger](#logger)
7. [Exporter](#exporter)
8. [MemoryPool](#memorypool)
9. [RingBuffer](#ringbuffer)

---

## Order Types

All types live in the `lob` namespace. Include `lob/Order.hpp`.

### Enumerations

| Type          | Values                                                 | Notes               |
| ------------- | ------------------------------------------------------ | ------------------- |
| `Side`        | `BUY`, `SELL`                                          | Stored as `uint8_t` |
| `OrderType`   | `LIMIT`, `MARKET`, `IOC`, `FOK`                        | Stored as `uint8_t` |
| `OrderStatus` | `ACTIVE`, `PARTIAL`, `FILLED`, `CANCELLED`, `REJECTED` | Set by the engine   |

### struct Trade

| Field           | Type                  | Description                          |
| --------------- | --------------------- | ------------------------------------ |
| `buy_order_id`  | `uint64_t`            | ID of the buy-side order             |
| `sell_order_id` | `uint64_t`            | ID of the sell-side order            |
| `price`         | `double`              | Execution price (passive side price) |
| `quantity`      | `uint64_t`            | Shares or contracts traded           |
| `timestamp`     | `chrono::nanoseconds` | Wall-clock time of execution         |

### struct Order

| Field             | Type                  | Description                       |
| ----------------- | --------------------- | --------------------------------- |
| `id`              | `uint64_t`            | Caller-assigned unique identifier |
| `side`            | `Side`                | BUY or SELL                       |
| `type`            | `OrderType`           | LIMIT, MARKET, IOC, or FOK        |
| `status`          | `OrderStatus`         | Maintained by the engine          |
| `price`           | `double`              | Limit price (0 for MARKET orders) |
| `quantity`        | `uint64_t`            | Original total quantity           |
| `filled_quantity` | `uint64_t`            | Cumulative quantity filled so far |
| `symbol`          | `std::string`         | Instrument identifier             |
| `timestamp`       | `chrono::nanoseconds` | Submission timestamp              |

### Order helper methods

| Method         | Return     | Description                             |
| -------------- | ---------- | --------------------------------------- |
| `remaining()`  | `uint64_t` | `quantity - filled_quantity`            |
| `is_active()`  | `bool`     | True if status is ACTIVE or PARTIAL     |
| `is_buy()`     | `bool`     | True if side is BUY                     |
| `is_sell()`    | `bool`     | True if side is SELL                    |
| `fill_ratio()` | `double`   | `filled_quantity / quantity`, in [0, 1] |

---

## OrderBook

Include `lob/OrderBook.hpp`. Constructed with a symbol string.

### Mutation methods

| Method                                        | Return | Description                                                |
| --------------------------------------------- | ------ | ---------------------------------------------------------- |
| `add_order(Order)`                            | `bool` | Add a resting order. Returns false if ID already exists    |
| `cancel_order(uint64_t id)`                   | `bool` | Cancel by ID. Returns false if not found or not active     |
| `modify_order(uint64_t id, uint64_t new_qty)` | `bool` | Change total quantity. new_qty must exceed filled_quantity |
| `reset()`                                     | `void` | Remove all resting orders and clear all maps               |

### Price and spread queries

| Method              | Return             | Description                 |
| ------------------- | ------------------ | --------------------------- |
| `best_bid()`        | `optional<double>` | Highest resting bid price   |
| `best_ask()`        | `optional<double>` | Lowest resting ask price    |
| `mid_price()`       | `optional<double>` | `(best_bid + best_ask) / 2` |
| `spread()`          | `optional<double>` | `best_ask - best_bid`       |
| `relative_spread()` | `optional<double>` | `spread / mid_price`        |

### Depth and analytics

| Method                                | Return             | Description                                                     |
| ------------------------------------- | ------------------ | --------------------------------------------------------------- |
| `bid_depth()`                         | `uint64_t`         | Total quantity across all bid levels                            |
| `ask_depth()`                         | `uint64_t`         | Total quantity across all ask levels                            |
| `order_count()`                       | `size_t`           | Number of resting orders                                        |
| `level_count_bids()`                  | `size_t`           | Number of distinct bid price levels                             |
| `level_count_asks()`                  | `size_t`           | Number of distinct ask price levels                             |
| `imbalance()`                         | `double`           | `(bid - ask) / (bid + ask)`, in [-1, 1]                         |
| `bid_vwap(levels)`                    | `VWAPResult`       | VWAP over top N bid levels                                      |
| `ask_vwap(levels)`                    | `VWAPResult`       | VWAP over top N ask levels                                      |
| `available_qty_at_price(side, price)` | `uint64_t`         | Cumulative depth at-or-better than price                        |
| `estimate_market_impact(side, qty)`   | `optional<double>` | Estimated average fill price; nullopt if insufficient liquidity |
| `snapshot(levels)`                    | `BookSnapshot`     | Top N levels on each side                                       |
| `find_order(id)`                      | `const Order*`     | Pointer into orders map, or nullptr                             |

### struct BookSnapshot

| Field       | Type                             | Description                          |
| ----------- | -------------------------------- | ------------------------------------ |
| `symbol`    | `string`                         | Instrument                           |
| `bids`      | `vector<pair<double, uint64_t>>` | Price and total quantity, descending |
| `asks`      | `vector<pair<double, uint64_t>>` | Price and total quantity, ascending  |
| `timestamp` | `chrono::nanoseconds`            | Time of snapshot                     |

### struct VWAPResult

| Field       | Type       | Description                         |
| ----------- | ---------- | ----------------------------------- |
| `vwap`      | `double`   | Volume-weighted average price       |
| `total_qty` | `uint64_t` | Total quantity summed across levels |
| `valid`     | `bool`     | False if the book side is empty     |

---

## MatchingEngine

Include `lob/MatchingEngine.hpp`.

### Order lifecycle

| Method                          | Return               | Description                                                       |
| ------------------------------- | -------------------- | ----------------------------------------------------------------- |
| `register_symbol(symbol)`       | `void`               | Pre-register a symbol (optional; auto-registered on first submit) |
| `has_symbol(symbol)`            | `bool`               | True if the symbol has a registered book                          |
| `submit_order(Order)`           | `MatchResult`        | Match and/or rest the order                                       |
| `cancel_order(symbol, id)`      | `bool`               | Cancel a resting order                                            |
| `modify_order(symbol, id, qty)` | `bool`               | Modify quantity of a resting order                                |
| `reset_book(symbol)`            | `void`               | Reset a single symbol's book                                      |
| `reset_all_books()`             | `void`               | Reset every registered book                                       |
| `reset_stats()`                 | `void`               | Zero all EngineStats counters                                     |
| `get_book(symbol)`              | `const OrderBook*`   | Read-only access to a book, or nullptr                            |
| `stats()`                       | `const EngineStats&` | Cumulative statistics reference                                   |

### Callbacks

| Setter                    | Callback signature                              | Fires when               |
| ------------------------- | ----------------------------------------------- | ------------------------ |
| `set_trade_callback(cb)`  | `void(const Trade&)`                            | Every execution          |
| `set_fill_callback(cb)`   | `void(uint64_t id, uint64_t qty, double price)` | Both sides of every fill |
| `set_reject_callback(cb)` | `void(uint64_t id, const string& reason)`       | Order rejected           |

### struct MatchResult

| Field           | Type            | Description                                            |
| --------------- | --------------- | ------------------------------------------------------ |
| `trades`        | `vector<Trade>` | All executions produced                                |
| `resting`       | `bool`          | True if the order (or remainder) was added to the book |
| `fully_filled`  | `bool`          | True if the aggressor was 100% consumed                |
| `rejected`      | `bool`          | True if the order was rejected without any execution   |
| `reject_reason` | `string`        | Human-readable rejection message                       |

### struct EngineStats

| Field              | Type       | Description                             |
| ------------------ | ---------- | --------------------------------------- |
| `orders_processed` | `uint64_t` | Total submit_order calls                |
| `orders_matched`   | `uint64_t` | Aggressors fully filled                 |
| `orders_resting`   | `uint64_t` | Orders that rested (at least partially) |
| `orders_cancelled` | `uint64_t` | Successful cancel_order calls           |
| `orders_rejected`  | `uint64_t` | Rejected orders (FOK, etc.)             |
| `total_trades`     | `uint64_t` | Number of Trade objects created         |
| `total_volume`     | `uint64_t` | Cumulative shares traded                |
| `total_notional`   | `double`   | Cumulative price x quantity             |

### Order type behaviour

| Order type | Liquidity check                      | On partial fill          | Rests |
| ---------- | ------------------------------------ | ------------------------ | ----- |
| LIMIT      | Price must cross the opposite best   | Remainder rests          | Yes   |
| MARKET     | No price check                       | Partial discard, no rest | Never |
| IOC        | Price must cross                     | Remainder cancelled      | Never |
| FOK        | Checks full qty before touching book | Rejected if insufficient | Never |

---

## FeedHandler

Include `lob/FeedHandler.hpp`. Constructed with a `MatchingEngine&` reference.

### Methods

| Method                                | Return                 | Description                                    |
| ------------------------------------- | ---------------------- | ---------------------------------------------- |
| `configure(FeedConfig)`               | `void`                 | Set config and register symbol                 |
| `load_lobster_csv(path)`              | `vector<LOBSTEREvent>` | Parse LOBSTER message CSV                      |
| `replay_lobster(events, symbol)`      | `void`                 | Feed events into the engine                    |
| `generate_synthetic(SyntheticConfig)` | `void`                 | Generate and submit synthetic orders           |
| `set_event_callback(cb)`              | `void`                 | Fire `cb` for each LOBSTER event               |
| `set_order_callback(cb)`              | `void`                 | Fire `cb` for each new order before submission |
| `events_processed()`                  | `uint64_t`             | Counter since last reset                       |
| `reset()`                             | `void`                 | Zero event counter and order-ID sequence       |

### struct SyntheticConfig

| Field          | Type       | Default | Description                        |
| -------------- | ---------- | ------- | ---------------------------------- |
| `mid_price`    | `double`   | 100.0   | Initial mid price                  |
| `tick_size`    | `double`   | 0.01    | Minimum price increment            |
| `arrival_rate` | `double`   | 1000.0  | Poisson lambda (orders per second) |
| `cancel_rate`  | `double`   | 0.4     | Probability each event is a cancel |
| `price_std`    | `double`   | 0.05    | Standard deviation of price noise  |
| `min_qty`      | `uint64_t` | 1       | Minimum order size                 |
| `max_qty`      | `uint64_t` | 100     | Maximum order size                 |
| `num_events`   | `uint64_t` | 100000  | Total events to generate           |
| `seed`         | `uint32_t` | 42      | RNG seed for reproducibility       |
| `spread_ticks` | `int`      | 2       | Initial half-spread in ticks       |

### LOBSTER event types

| Event type | Meaning                          | Engine action     |
| ---------- | -------------------------------- | ----------------- |
| 1          | New limit order                  | submit_order      |
| 2          | Partial cancel (size reduction)  | modify_order      |
| 3          | Full cancel / deletion           | cancel_order      |
| 4          | Visible execution (passive side) | cancel_order      |
| 5          | Hidden execution                 | Skipped           |
| 7          | Trading halt                     | Log warning, skip |

---

## LatencyRecorder and ScopedTimer

Include `lob/Latency.hpp`.

### LatencyRecorder methods

| Method                | Return     | Description                                        |
| --------------------- | ---------- | -------------------------------------------------- |
| `record(nanoseconds)` | `void`     | Append one sample                                  |
| `clear()`             | `void`     | Discard all samples                                |
| `count()`             | `size_t`   | Number of recorded samples                         |
| `sample(idx)`         | `uint64_t` | Raw sample in nanoseconds (throws on out-of-range) |
| `mean_ns()`           | `double`   | Arithmetic mean                                    |
| `stddev_ns()`         | `double`   | Sample standard deviation                          |
| `min_ns()`            | `uint64_t` | Minimum sample                                     |
| `max_ns()`            | `uint64_t` | Maximum sample                                     |
| `p50_ns()`            | `uint64_t` | 50th percentile (median)                           |
| `p90_ns()`            | `uint64_t` | 90th percentile                                    |
| `p99_ns()`            | `uint64_t` | 99th percentile                                    |
| `p999_ns()`           | `uint64_t` | 99.9th percentile                                  |
| `percentile_ns(p)`    | `uint64_t` | Arbitrary percentile p in [0, 100]                 |
| `merge(other)`        | `void`     | Append all samples from another recorder           |

### ScopedTimer

```cpp
LatencyRecorder rec;
{
    ScopedTimer t{rec};    // clock starts here
    // ... measured code ...
}                          // clock stops, sample appended to rec
```

---

## Logger

Include `lob/Logger.hpp`. Singleton accessed via `Logger::instance()`.

| Method                           | Description                                                |
| -------------------------------- | ---------------------------------------------------------- |
| `set_level(LogLevel)`            | Filter below this level (DEBUG=0, INFO=1, WARN=2, ERROR=3) |
| `level()`                        | Current minimum level                                      |
| `log(level, component, message)` | Write one log line to stderr                               |
| `set_output(FILE*)`              | Redirect to a file; pass nullptr to revert to stderr       |

### Convenience macros

| Macro                  | Level |
| ---------------------- | ----- |
| `LOB_DEBUG(comp, msg)` | DEBUG |
| `LOB_INFO(comp, msg)`  | INFO  |
| `LOB_WARN(comp, msg)`  | WARN  |
| `LOB_ERROR(comp, msg)` | ERROR |

---

## Exporter

Include `lob/Exporter.hpp`. All methods are `static`.

| Method                                 | Output                    | Description                      |
| -------------------------------------- | ------------------------- | -------------------------------- |
| `export_snapshot(snap, path)`          | `side,price,quantity` CSV | Book snapshot                    |
| `export_latency(rec, path)`            | `latency_ns` CSV          | One sample per line              |
| `export_latency_summary(rec, path)`    | key=value text            | Mean, stddev, all percentiles    |
| `export_stats(stats, path)`            | key=value text            | EngineStats fields               |
| `write_trade_header(ofstream&)`        | CSV header line           | Call once before writing trades  |
| `write_trade(ofstream&, trade)`        | One CSV row               | Streaming trade append           |
| `write_timeseries_header(ofstream&)`   | CSV header line           | For time-series snapshot stream  |
| `append_snapshot_row(ofstream&, snap)` | Multiple CSV rows         | One row per price level per side |

---

## MemoryPool

Include `lob/MemoryPool.hpp`. Template: `MemoryPool<T, Capacity>`.

| Method               | Return   | Description                                           |
| -------------------- | -------- | ----------------------------------------------------- |
| `allocate()`         | `T*`     | Return raw storage for one T, or nullptr if exhausted |
| `deallocate(T* p)`   | `void`   | Destroy T and return slot to pool; safe on nullptr    |
| `construct(args...)` | `T*`     | allocate + placement-new                              |
| `destroy(T* p)`      | `void`   | Alias for deallocate                                  |
| `available()`        | `size_t` | Slots currently available                             |
| `capacity()`         | `size_t` | Compile-time capacity constant                        |

Thread safety: MPMC safe for concurrent `allocate`/`deallocate` via a lock-free CAS free-stack.

---

## RingBuffer

Include `lob/RingBuffer.hpp`. Template: `RingBuffer<T, Capacity>`. Capacity must be a power of two.

| Method           | Return        | Description                                   |
| ---------------- | ------------- | --------------------------------------------- |
| `push(const T&)` | `bool`        | Producer: enqueue item. Returns false if full |
| `push(T&&)`      | `bool`        | Producer: move-enqueue item                   |
| `pop()`          | `optional<T>` | Consumer: dequeue item, or nullopt if empty   |
| `empty()`        | `bool`        | Approximate emptiness check                   |
| `size()`         | `size_t`      | Approximate item count                        |
| `capacity()`     | `size_t`      | Compile-time capacity (usable = capacity - 1) |

Thread safety: SPSC only. Exactly one producer thread and one consumer thread.
