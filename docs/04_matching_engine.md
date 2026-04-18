# Matching Engine Internals

## Table of Contents

1. [Price-Time Priority](#price-time-priority)
2. [Order Type Semantics](#order-type-semantics)
3. [Crossing Algorithm](#crossing-algorithm)
4. [Book State Invariants](#book-state-invariants)
5. [Execution Price Rules](#execution-price-rules)
6. [Statistics Accounting](#statistics-accounting)
7. [Performance Characteristics](#performance-characteristics)

---

## Price-Time Priority

QuantLOB implements standard price-time (FIFO) priority matching.

| Priority dimension | Rule                                                                                                        |
| ------------------ | ----------------------------------------------------------------------------------------------------------- |
| Price (primary)    | Better price is matched first. Best ask (lowest) for buy aggressors; best bid (highest) for sell aggressors |
| Time (secondary)   | Among orders at the same price, the oldest submission is matched first                                      |
| Quantity           | Has no effect on priority; large orders do not jump the queue                                               |

The bid side is stored in a `std::map<double, PriceLevel, std::greater<double>>` so that
`begin()` always points to the best (highest) bid. The ask side uses `std::less<double>` so
that `begin()` points to the best (lowest) ask. Within each `PriceLevel`, orders are held in
a `std::list<uint64_t>` whose front is always the oldest order at that price.

---

## Order Type Semantics

### LIMIT

1. Cross against the opposing side while a matching price exists and quantity remains.
2. Any unfilled remainder is added to the book as a resting order.

| Scenario          | Outcome                                  |
| ----------------- | ---------------------------------------- |
| No crossing price | Entire order rests                       |
| Partial cross     | Filled portion executed; remainder rests |
| Full cross        | Fully executed; nothing rests            |

### MARKET

1. Cross against the opposing side with no price constraint.
2. Any unfilled remainder is discarded silently.

| Scenario          | Outcome                                         |
| ----------------- | ----------------------------------------------- |
| Empty book        | Nothing executed; order discarded               |
| Partial liquidity | Available portion executed; remainder discarded |
| Full liquidity    | Fully executed                                  |

### IOC (Immediate-Or-Cancel)

Identical to MARKET except: the unfilled remainder is explicitly cancelled
(status set to CANCELLED) rather than silently discarded. No order ever rests.

### FOK (Fill-Or-Kill)

1. Compute available liquidity at-or-better than the limit price without touching the book.
2. If available quantity is less than order quantity, reject the entire order (no trade occurs).
3. If sufficient, proceed identically to a LIMIT cross.

| Liquidity check result | Outcome                                         |
| ---------------------- | ----------------------------------------------- |
| Available < required   | Rejected; book untouched; reject callback fires |
| Available >= required  | Fully filled in a single crossing pass          |

---

## Crossing Algorithm

The `cross()` function in `MatchingEngine.cpp` walks the opposing side's price
levels in priority order. Within each level it walks the `order_ids` list in FIFO order.

```
for each level (sorted by price priority):
    if limit price constraint violated: break
    for each order_id in level.order_ids (FIFO):
        fill_qty = min(aggressor.remaining, passive.remaining)
        if fill_qty == 0: skip
        emit Trade(aggressor, passive, passive.price, fill_qty)
        update aggressor filled_quantity and status
        if passive fully consumed:
            apply_fill(passive_id, fill_qty)     -- updates status and level total_quantity
            erase passive_id from order_ids list
            erase passive Order from orders_ map
        else:
            apply_fill(passive_id, fill_qty)     -- partial fill; order stays in list
    if level.order_ids is now empty: erase level from map
```

### Consistency invariants maintained during crossing

| Invariant                               | How it is maintained                                                   |
| --------------------------------------- | ---------------------------------------------------------------------- |
| `order_ids` list matches `orders_` map  | Fully consumed orders are erased from both in the same iteration       |
| `PriceLevel.total_quantity` is accurate | `apply_fill` decrements it for every fill                              |
| Empty price levels are removed          | Level erased immediately after its last order is consumed              |
| Stale IDs in order_ids list             | Guarded by a `orders_.find()` check; stale entries are erased in-place |

---

## Book State Invariants

After every `submit_order`, `cancel_order`, or `modify_order` call the following
invariants must hold:

| Invariant           | Description                                                                          |
| ------------------- | ------------------------------------------------------------------------------------ |
| No crossed book     | `best_bid < best_ask` at all times after a complete operation                        |
| ID uniqueness       | Each order ID appears at most once across all price levels                           |
| Level consistency   | `PriceLevel.total_quantity == sum(orders_[id].remaining() for id in order_ids)`      |
| FIFO within level   | `order_ids.front()` has the smallest submission timestamp among all IDs in the level |
| Active-only resting | Every ID in any `order_ids` list maps to an ACTIVE or PARTIAL order in `orders_`     |

---

## Execution Price Rules

| Aggressor type       | Execution price        | Rationale                                                   |
| -------------------- | ---------------------- | ----------------------------------------------------------- |
| BUY limit or MARKET  | Passive ask price      | Buyer gets price improvement if crossing below their limit  |
| SELL limit or MARKET | Passive bid price      | Seller gets price improvement if crossing above their limit |
| IOC                  | Same as LIMIT / MARKET | IOC is a sweep with immediate cancel on remainder           |
| FOK                  | Same as LIMIT          | FOK verified before touching the book                       |

Price improvement: if a BUY limit order has price 101.00 and the best ask is 100.00, the trade
executes at 100.00, not 101.00. The passive side's price always wins.

---

## Statistics Accounting

| Counter            | Incremented when                                          |
| ------------------ | --------------------------------------------------------- |
| `orders_processed` | Every `submit_order` call                                 |
| `orders_matched`   | Aggressor is fully filled (`result.fully_filled == true`) |
| `orders_resting`   | Aggressor rested (at least partially) on the book         |
| `orders_cancelled` | A `cancel_order` call returns true                        |
| `orders_rejected`  | `result.rejected == true`                                 |
| `total_trades`     | `result.trades.size()` is added                           |
| `total_volume`     | Each trade's quantity is added                            |
| `total_notional`   | Each trade's `price * quantity` is added                  |

---

## Performance Characteristics

All complexities are worst-case unless noted.

| Operation                                 | Time complexity | Notes                                                  |
| ----------------------------------------- | --------------- | ------------------------------------------------------ |
| `add_order`                               | O(log L)        | L = number of price levels on the side                 |
| `cancel_order`                            | O(log L + Q)    | Q = orders at the cancelled price level (list removal) |
| `modify_order`                            | O(log L)        | No list manipulation needed                            |
| `submit_order` (no cross)                 | O(log L)        | Just `add_order`                                       |
| `submit_order` (cross N levels, M orders) | O(N log L + M)  | N levels swept, M passive orders consumed              |
| `snapshot(K)`                             | O(K)            | Simply iterates the first K entries in each sorted map |
| `bid_depth` / `ask_depth`                 | O(L)            | Sums `total_quantity` across all levels                |
| `imbalance`                               | O(L)            | Requires bid_depth and ask_depth                       |
| `bid_vwap(K)` / `ask_vwap(K)`             | O(K)            | Only touches the first K levels                        |
| `estimate_market_impact`                  | O(L)            | May scan all levels in the worst case                  |

For a typical NASDAQ stock with 10--50 active price levels, all operations are effectively O(1)
to O(log 50) in practice.
