# Data Formats Reference

## Table of Contents

1. [LOBSTER Message CSV](#lobster-message-csv)
2. [Synthetic Feed Configuration](#synthetic-feed-configuration)
3. [Snapshot CSV](#snapshot-csv)
4. [Trade Log CSV](#trade-log-csv)
5. [LOB Time-Series CSV](#lob-time-series-csv)
6. [Latency CSV](#latency-csv)
7. [Latency Summary Text](#latency-summary-text)
8. [Stats Text](#stats-text)
9. [ML Weight Files](#ml-weight-files)

---

## LOBSTER Message CSV

LOBSTER (Limit Order Book System -- The Efficient Reconstruction) is a
widely-used academic dataset for US equity LOBs. Each row in the message
file describes a single market event.

### Column layout (no header row in the original format)

| Column       | Index | Type   | Description                                                  |
| ------------ | ----- | ------ | ------------------------------------------------------------ |
| `time`       | 0     | float  | Seconds since midnight (e.g., 34200.0 = 09:30:00)            |
| `event_type` | 1     | int    | Event code; see table below                                  |
| `order_id`   | 2     | uint64 | Exchange-assigned order identifier                           |
| `size`       | 3     | uint64 | Quantity in shares                                           |
| `price`      | 4     | int    | Price as integer scaled by 10000 (e.g., 1000000 = $100.0000) |
| `direction`  | 5     | int    | 1 = buy, -1 = sell                                           |

### Event type codes

| Code | Name                                     | Engine action       |
| ---- | ---------------------------------------- | ------------------- |
| 1    | New limit order                          | submit_order(LIMIT) |
| 2    | Partial cancellation (size reduction)    | modify_order        |
| 3    | Full cancellation / deletion             | cancel_order        |
| 4    | Visible execution (passive side removed) | cancel_order        |
| 5    | Hidden execution                         | Skipped             |
| 7    | Trading halt                             | Logged, skipped     |

### Example rows

```
34200.005123456,1,5765,100,1001500,-1
34200.005234567,1,5766,50,999800,1
34200.010000000,3,5765,100,1001500,-1
34200.015000000,4,5766,50,999800,1
```

### Parser behaviour

| Situation                           | Action                        |
| ----------------------------------- | ----------------------------- |
| File not found                      | Throws `std::runtime_error`   |
| First row starts with T, t, or #    | Treated as header and skipped |
| Row has fewer than 6 fields         | WARN log, row skipped         |
| Any field fails conversion          | WARN log, row skipped         |
| Trailing `\r` (Windows line ending) | Stripped automatically        |

---

## Synthetic Feed Configuration

Configurable via `SyntheticConfig` struct or CLI flags.

| Parameter      | CLI flag    | Default | Valid range | Effect                                                   |
| -------------- | ----------- | ------- | ----------- | -------------------------------------------------------- |
| `mid_price`    | `--mid`     | 100.0   | > tick_size | Initial mid price                                        |
| `tick_size`    | `--tick`    | 0.01    | > 0         | Minimum price increment; prices are snapped to multiples |
| `arrival_rate` | (code only) | 1000.0  | > 0         | Poisson lambda; higher values = faster events            |
| `cancel_rate`  | (code only) | 0.4     | [0, 1]      | Fraction of events that are random cancellations         |
| `price_std`    | (code only) | 0.05    | >= 0        | Gaussian noise standard deviation for price offsets      |
| `min_qty`      | (code only) | 1       | >= 1        | Lower bound for uniform order size distribution          |
| `max_qty`      | (code only) | 100     | >= min_qty  | Upper bound                                              |
| `num_events`   | `--events`  | 100000  | >= 1        | Total events (orders + cancels)                          |
| `seed`         | `--seed`    | 42      | any uint32  | RNG seed; same seed = same sequence                      |
| `spread_ticks` | (code only) | 2       | >= 0        | Half-spread for initial price placement                  |

---

## Snapshot CSV

Written by `Exporter::export_snapshot()`.

### Schema

| Column     | Type        | Description                  |
| ---------- | ----------- | ---------------------------- |
| `side`     | string      | BID or ASK                   |
| `price`    | float (6dp) | Price level                  |
| `quantity` | uint64      | Total quantity at this level |

### Example

```csv
side,price,quantity
BID,99.990000,500
BID,99.980000,1200
BID,99.970000,800
ASK,100.010000,600
ASK,100.020000,900
```

---

## Trade Log CSV

Written by `Exporter::write_trade_header()` + `Exporter::write_trade()`.

### Schema

| Column          | Type        | Description                          |
| --------------- | ----------- | ------------------------------------ |
| `timestamp_ns`  | int64       | Nanoseconds since Unix epoch         |
| `buy_order_id`  | uint64      | ID of the aggressive or resting buy  |
| `sell_order_id` | uint64      | ID of the aggressive or resting sell |
| `price`         | float (6dp) | Execution price                      |
| `quantity`      | uint64      | Shares traded                        |
| `notional`      | float (2dp) | `price * quantity`                   |

### Example

```csv
timestamp_ns,buy_order_id,sell_order_id,price,quantity,notional
1713398400000000000,1000000042,1000000017,100.010000,50,5000.50
1713398400000100000,1000000043,1000000018,100.000000,200,20000.00
```

---

## LOB Time-Series CSV

Written by `Exporter::write_timeseries_header()` + `Exporter::append_snapshot_row()`.
Produced by the `--export-timeseries` flag at intervals of `--snap-interval` events.

### Schema

| Column         | Type        | Description                                   |
| -------------- | ----------- | --------------------------------------------- |
| `timestamp_ns` | int64       | Nanoseconds since Unix epoch at snapshot time |
| `side`         | string      | BID or ASK                                    |
| `price`        | float (6dp) | Price level                                   |
| `quantity`     | uint64      | Total quantity at this level                  |

### Example

```csv
timestamp_ns,side,price,quantity
1713398400000000000,BID,99.990000,500
1713398400000000000,BID,99.980000,1200
1713398400000000000,ASK,100.010000,600
1713398400001000000,BID,99.990000,450
1713398400001000000,ASK,100.010000,700
```

---

## Latency CSV

Written by `Exporter::export_latency()`. One sample per row.

### Schema

| Column       | Type   | Description                       |
| ------------ | ------ | --------------------------------- |
| `latency_ns` | uint64 | Nanoseconds per measurement batch |

### Example

```csv
latency_ns
312
287
345
```

---

## Latency Summary Text

Written by `Exporter::export_latency_summary()`. Key=value format.

### Fields

| Key         | Type   | Description               |
| ----------- | ------ | ------------------------- |
| `count`     | uint64 | Number of samples         |
| `mean_ns`   | uint64 | Arithmetic mean           |
| `stddev_ns` | uint64 | Sample standard deviation |
| `min_ns`    | uint64 | Minimum                   |
| `p50_ns`    | uint64 | 50th percentile (median)  |
| `p90_ns`    | uint64 | 90th percentile           |
| `p99_ns`    | uint64 | 99th percentile           |
| `p999_ns`   | uint64 | 99.9th percentile         |
| `max_ns`    | uint64 | Maximum                   |

### Example

```
count=500
mean_ns=312
stddev_ns=48
min_ns=201
p50_ns=305
p90_ns=380
p99_ns=451
p999_ns=612
max_ns=843
```

---

## Stats Text

Written by `Exporter::export_stats()`. Key=value format.

### Fields

| Key                | Type   | Description                           |
| ------------------ | ------ | ------------------------------------- |
| `orders_processed` | uint64 | Total submit_order calls              |
| `orders_matched`   | uint64 | Fully filled aggressors               |
| `orders_resting`   | uint64 | Orders that rested at least partially |
| `orders_cancelled` | uint64 | Successful cancellations              |
| `orders_rejected`  | uint64 | Rejected orders                       |
| `total_trades`     | uint64 | Execution count                       |
| `total_volume`     | uint64 | Cumulative shares traded              |
| `total_notional`   | float  | Cumulative price x quantity           |

---

## ML Weight Files

JSON format exported by the Python training scripts and loaded by C++ models.

### MidPricePredictor weight file

```json
{
  "model": "MidPricePredictor",
  "version": "1.0",
  "feature_dim": 40,
  "learning_rate": 0.001,
  "regularisation": 0.01,
  "weights": [0.0023, -0.0041, 0.0017, ...],
  "bias": 0.00012,
  "n_updates": 150000
}
```

### OrderFlowPredictor weight file

```json
{
  "model": "OrderFlowPredictor",
  "version": "1.0",
  "feature_dim": 40,
  "learning_rate": 0.005,
  "regularisation": 0.01,
  "weights": [-0.012, 0.034, ...],
  "bias": -0.005,
  "n_updates": 150000
}
```

### AnomalyDetector state file

```json
{
  "model": "AnomalyDetector",
  "version": "1.0",
  "feature_dim": 40,
  "ewma_alpha": 0.05,
  "threshold_sigma": 3.5,
  "ewma_mean": [99.5, 0.0012, ...],
  "ewma_variance": [0.0025, 0.000001, ...],
  "n_updates": 150000
}
```
