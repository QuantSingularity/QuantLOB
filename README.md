# QuantLOB -- High-Performance Limit Order Book Simulator

A C++20 limit order book (LOB) engine with price-time priority matching,
LOBSTER data replay, synthetic feed generation, latency profiling, machine
learning feature extraction, and visualization tooling.

---

## Project Layout

```
QuantLOB/
|-- CMakeLists.txt
|-- README.md
|-- LICENSE
|-- docs/                       Full reference documentation (8 documents)
|-- code/
|   |-- include/lob/            Core C++ public headers
|   |-- src/                    Core implementation files
|   |-- tests/                  Catch2 unit tests (core)
|   |-- benchmarks/             Google Benchmark suite
|   |-- data/sample/            LOBSTER CSV data directory
|   `-- ai_models/                     Machine learning module
|       |-- include/lob/ai_models/     ML public headers
|       |-- src/                ML implementation files
|       |-- python/             Offline training and evaluation scripts
|       `-- tests/              ML unit tests
|-- infrastructure/
|   |-- cmake/                  Compiler warning and sanitizer helpers
|   `-- docker/                 Docker build context
`-- scripts/
    |-- python/                 Visualisation and sample data generation
    `-- shell/                  Build and test helpers
```

---

## Documentation

See `docs/README.md` for the full documentation index.

| Document                           | Contents                                       |
| ---------------------------------- | ---------------------------------------------- |
| docs/01_architecture.md            | Component map, data flow, threading model      |
| docs/02_api_reference.md           | Complete public API for every class            |
| docs/03_build_and_configuration.md | Prerequisites, CMake flags, sanitizers, Docker |
| docs/04_matching_engine.md         | Crossing algorithm, invariants, complexity     |
| docs/05_ml_module.md               | Feature extractor, models, Python training     |
| docs/06_data_formats.md            | All CSV and text formats                       |
| docs/07_performance_guide.md       | Throughput targets, profiling workflow         |
| docs/08_testing_guide.md           | All test cases, sanitizer runs, CI             |

---

## Quick Start

```bash
# Build (Release)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run synthetic simulation (500k events)
./build/quantlob

# Run with exports
./build/quantlob --events 200000 \
    --export-trades --export-snapshot --export-latency --export-timeseries \
    --out-dir out

# Run tests
cd build && ctest --output-on-failure

# Run benchmarks
./build/quantlob_bench
```

---

## CMake Options

| Option                      | Default | Description                                    |
| --------------------------- | ------- | ---------------------------------------------- |
| `QUANTLOB_BUILD_TESTS`      | ON      | Catch2 test binary                             |
| `QUANTLOB_BUILD_BENCHMARKS` | ON      | Google Benchmark binary                        |
| `QUANTLOB_BUILD_MAIN`       | ON      | CLI driver                                     |
| `QUANTLOB_BUILD_AI_MODELS`  | ON      | ML module (FeatureExtractor, models, pipeline) |
| `QUANTLOB_ENABLE_ASAN`      | OFF     | AddressSanitizer                               |
| `QUANTLOB_ENABLE_TSAN`      | OFF     | ThreadSanitizer                                |
| `QUANTLOB_ENABLE_UBSAN`     | OFF     | UBSanitizer                                    |
| `QUANTLOB_ENABLE_LTO`       | OFF     | Link-time optimisation                         |

---

## Core Features

### Matching Engine

| Order type | Behaviour                                                 |
| ---------- | --------------------------------------------------------- |
| LIMIT      | Cross then rest remainder                                 |
| MARKET     | Cross with no price constraint; discard remainder         |
| IOC        | Cross then cancel remainder                               |
| FOK        | Reject if full quantity unavailable; otherwise fully fill |

### Order Book Analytics

| Method                                     | Description                                                  |
| ------------------------------------------ | ------------------------------------------------------------ |
| `best_bid / best_ask / mid_price / spread` | Basic price queries                                          |
| `relative_spread`                          | Spread as fraction of mid                                    |
| `imbalance`                                | (bid_depth - ask_depth) / (bid_depth + ask_depth)            |
| `bid_vwap / ask_vwap`                      | Volume-weighted average price over top N levels              |
| `estimate_market_impact`                   | Estimated average fill price for a hypothetical market order |
| `available_qty_at_price`                   | Cumulative depth at or better than a price                   |

### Machine Learning Module

| Model              | Algorithm                        | Task                                    |
| ------------------ | -------------------------------- | --------------------------------------- |
| FeatureExtractor   | Rolling analytics                | 40-dim feature vector from LOB state    |
| MidPricePredictor  | Online ridge regression (SGD)    | Predict next mid-price change           |
| OrderFlowPredictor | Online logistic regression (SGD) | Predict next order direction (BUY/SELL) |
| AnomalyDetector    | EWMA Z-score                     | Flag abnormal LOB states                |
| MLPipeline         | Orchestrator                     | Runs all models per tick                |

---

## CLI Reference

```
Options:
  --symbol SYM         Instrument symbol (default: AAPL)
  --lobster MSG.csv    Replay LOBSTER message CSV
  --realtime           Enable real-time replay pacing
  --speed FACTOR       Replay speed multiplier (default: 1.0)
  --events N           Synthetic event count (default: 500000)
  --mid PRICE          Synthetic initial mid price (default: 150.0)
  --tick SIZE          Tick size (default: 0.01)
  --seed N             RNG seed (default: 12345)
  --levels N           Snapshot depth (default: 5)
  --out-dir DIR        Output directory for CSV/text exports
  --export-trades      Export trade log to CSV
  --export-snapshot    Export final order book snapshot
  --export-latency     Export per-order latency samples + summary
  --export-timeseries  Export periodic LOB snapshot time-series
  --snap-interval N    Events between time-series snapshots (default: 1000)
  --log-level LEVEL    DEBUG|INFO|WARN|ERROR (default: INFO)
  --help               Show this help
```

---

## Python Tools

```bash
# Generate a synthetic LOBSTER-format CSV
python3 scripts/python/generate_sample_data.py

# Visualise output from an export run
python3 scripts/python/visualize_lob.py --dir out

# Train mid-price predictor offline
python3 code/ai_models/python/train_mid_price.py \
    --data out/lob_timeseries.csv \
    --weights out/mid_price_weights.json

# Train order-flow predictor offline
python3 code/ai_models/python/train_order_flow.py \
    --trades out/trades.csv \
    --snapshot out/lob_timeseries.csv \
    --weights out/order_flow_weights.json

# Evaluate both models
python3 code/ai_models/python/evaluate_models.py \
    --data out/lob_timeseries.csv \
    --mid-weights out/mid_price_weights.json \
    --flow-weights out/order_flow_weights.json
```

---

## LOBSTER Data Format

Comma-separated, no header row. Columns:

| Index | Field      | Type   | Notes                                                       |
| ----- | ---------- | ------ | ----------------------------------------------------------- |
| 0     | time       | float  | Seconds since midnight                                      |
| 1     | event_type | int    | 1=new, 2=partial-cancel, 3=delete, 4=exec, 5=hidden, 7=halt |
| 2     | order_id   | uint64 | Exchange-assigned ID                                        |
| 3     | size       | uint64 | Shares                                                      |
| 4     | price      | int    | Integer scaled x10000 (e.g. 1000000 = $100.00)              |
| 5     | direction  | int    | 1=buy, -1=sell                                              |

---

## License

MIT -- see `LICENSE`.
