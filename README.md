# QuantLOB

A high-performance limit order book simulator. The core matching engine is modern
C++20, exposed through an interactive CLI, a JSON REST API, and a modern React and
TypeScript web terminal. Price-time priority matching, synthetic and LOBSTER feed
replay, nanosecond latency profiling, and an online machine learning pipeline are
all implemented from first principles in C++.

QuantLOB is built as a portfolio piece for quantitative and low-latency engineering
roles. The emphasis is on a correct, well-tested matching core and a clean
architecture rather than on breadth of half-finished features. Every simplifying
assumption is stated plainly in the Limitations table rather than hidden.

|                 |                                                                                      |
| --------------- | ------------------------------------------------------------------------------------ |
| **Language**    | C++20 (engine), TypeScript + React (web)                                             |
| **Interfaces**  | CLI simulator, REST API, web terminal                                                |
| **Core**        | OrderBook, MatchingEngine, FeedHandler, Latency, Exporter                            |
| **Order types** | LIMIT, MARKET, IOC, FOK                                                              |
| **ML module**   | FeatureExtractor, MidPricePredictor, OrderFlowPredictor, AnomalyDetector, MLPipeline |
| **Feeds**       | Synthetic Poisson order flow and LOBSTER message replay                              |
| **Tests**       | 131 unit tests, all passing via CTest (Catch2)                                       |
| **Build**       | CMake (engine), Vite (frontend), Docker (full stack)                                 |

## Capabilities

| Module          | What it does                                                                                                                                  |
| --------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Order Book      | Price-time priority book with best bid/ask/mid/spread, relative spread, imbalance, per-side VWAP, market-impact estimate, and top-N snapshots |
| Matching Engine | LIMIT, MARKET, IOC and FOK crossing with trade, reject and fill callbacks, plus cumulative engine statistics                                  |
| Feed Handler    | Synthetic Poisson order flow with Gaussian price noise, and LOBSTER message CSV replay with optional real-time pacing                         |
| Latency         | Per-sample recorder with mean, stddev, min, max and p50/p90/p99/p99.9, plus an RAII scoped timer                                              |
| ML Pipeline     | A 40-dimension feature extractor, online mid-price and order-flow predictors, and an EWMA anomaly detector, orchestrated per tick             |
| Exporter        | Snapshot, trade log, latency samples and summary, engine stats, and time-series CSVs consumed by the Python tools                             |
| Interfaces      | All of the above over a REST API and a live web terminal with a depth ladder and charts                                                       |

## Architecture

```
                      +---------------------------+
                      |  React + TypeScript UI     |
                      |  (Vite, Recharts)          |
                      +-------------+-------------+
                                    | fetch /api/*  (JSON over HTTP)
                                    v
                      +---------------------------+
                      |  REST API server           |
                      |  (cpp-httplib, nlohmann)   |
                      +-------------+-------------+
                                    | direct calls
                                    v
   +------------------------------------------------------------------+
   |                       QuantLOB engine (C++20)                     |
   |                                                                   |
   |  OrderBook    MatchingEngine    FeedHandler    ML Pipeline        |
   |  Utilities: Latency, Logger, Exporter, MemoryPool, RingBuffer     |
   +------------------------------------------------------------------+
                                    ^
                                    | same core, different front end
                      +---------------------------+
                      | CLI simulator + benchmarks |
                      +---------------------------+
```

The engine compiles once into two static libraries (`quantlob_core` and
`quantlob_ml_core`). The CLI simulator, the REST server, the test runner and the
benchmark harness all link against those libraries, so there is exactly one
implementation of every calculation.

## Project Structure

```
QuantLOB/
├── code/
│   ├── include/lob/            # Core engine headers
│   │   ├── Order.hpp           # Order, Trade, Side/OrderType/OrderStatus, as_string
│   │   ├── OrderBook.hpp       # Price-time priority book, snapshot, metrics, VWAP
│   │   ├── MatchingEngine.hpp  # LIMIT/MARKET/IOC/FOK matching, stats, callbacks
│   │   ├── FeedHandler.hpp     # Synthetic and LOBSTER feeds
│   │   ├── Latency.hpp         # Latency recorder and scoped timer
│   │   ├── Exporter.hpp        # CSV and text exporters
│   │   ├── Logger.hpp          # Thread-safe singleton logger
│   │   ├── MemoryPool.hpp      # Lock-free fixed-capacity pool
│   │   └── RingBuffer.hpp      # Single-producer/single-consumer ring buffer
│   ├── src/                    # Engine implementation (static lib quantlob_core)
│   ├── server/                 # REST API server (cpp-httplib + nlohmann/json)
│   ├── third_party/            # Vendored httplib and nlohmann/json (left untouched)
│   ├── tests/                  # Catch2 unit tests (core)
│   ├── benchmarks/             # Google Benchmark suite
│   ├── data/sample/            # LOBSTER CSV directory (generated, not committed)
│   └── ai_models/              # Machine learning module (static lib quantlob_ml_core)
│       ├── include/lob/ai_models/  # ML public headers
│       ├── src/                # ML implementation
│       ├── python/             # Offline training and evaluation scripts
│       └── tests/              # ML unit tests
├── frontend/                   # React + TypeScript + Vite web terminal
├── infrastructure/
│   ├── cmake/                  # Compiler warning and sanitizer helpers
│   └── docker/                 # Engine image, full-stack image, and compose files
├── scripts/
│   ├── python/                 # Sample data generation and visualisation
│   └── shell/                  # Build, test and full-stack run helpers
├── docs/                       # Eight reference documents (see docs/README.md)
├── CMakeLists.txt
└── README.md
```

## Matching engine

| Order type | Behaviour                                                                              |
| ---------- | -------------------------------------------------------------------------------------- |
| LIMIT      | Cross against the opposing side, then rest the remainder on the book                   |
| MARKET     | Cross with no price constraint; discard any unfilled remainder                         |
| IOC        | Cross immediately, then cancel any remainder (immediate-or-cancel)                     |
| FOK        | Reject unless the full quantity is available; otherwise fill completely (fill-or-kill) |

Crossing is strict price-time priority: best price first, then FIFO within a price
level. Aggressors and passive orders both fire fill callbacks, and every trade,
rejection and fill is reported through optional engine callbacks.

## Order book analytics

| Method                                           | Description                                                           |
| ------------------------------------------------ | --------------------------------------------------------------------- |
| `best_bid` / `best_ask` / `mid_price` / `spread` | Basic top-of-book queries (each optional, empty if the side is empty) |
| `relative_spread`                                | Spread as a fraction of the mid price                                 |
| `imbalance`                                      | `(bid_depth - ask_depth) / (bid_depth + ask_depth)`                   |
| `bid_vwap` / `ask_vwap`                          | Volume-weighted average price over the top N levels                   |
| `estimate_market_impact`                         | Estimated average fill price for a hypothetical market order          |
| `available_qty_at_price`                         | Cumulative depth at or better than a given price                      |
| `snapshot`                                       | Top-N depth ladder (bids descending, asks ascending)                  |

## Machine learning module

| Model              | Algorithm                        | Task                                         |
| ------------------ | -------------------------------- | -------------------------------------------- |
| FeatureExtractor   | Rolling microstructure analytics | A 40-dimension feature vector from LOB state |
| MidPricePredictor  | Online ridge regression (SGD)    | Predict the next mid-price change            |
| OrderFlowPredictor | Online logistic regression (SGD) | Predict the next order direction (BUY/SELL)  |
| AnomalyDetector    | EWMA z-score                     | Flag abnormal LOB states                     |
| MLPipeline         | Orchestrator                     | Run every model once per book tick           |

Offline trainers under `code/ai_models/python` consume the exported time-series and
trade CSVs and write weight JSON files loadable by the C++ predictors. The
mid-price trainer folds its internal feature standardisation into the saved
weights, so the exported model operates directly on the raw features the C++
`FeatureExtractor` produces.

## Modern C++ used

| Feature                             | Where it appears                                                  |
| ----------------------------------- | ----------------------------------------------------------------- |
| `std::optional`                     | Top-of-book values that may not exist (best bid/ask, mid, spread) |
| `std::map` with custom comparator   | Bid levels (descending) and ask levels (ascending)                |
| `std::list` per price level         | FIFO time priority within a price                                 |
| `std::function` callbacks           | Trade, reject and fill hooks on the engine                        |
| `<atomic>` and CAS                  | Lock-free free-stack in `MemoryPool`                              |
| `std::chrono` high-resolution clock | Nanosecond latency measurement                                    |
| `using namespace std;`              | Every translation unit (see Code style)                           |

## Prerequisites

| Tool          | Version              | Purpose                                         |
| ------------- | -------------------- | ----------------------------------------------- |
| C++ compiler  | GCC 13+ or Clang 16+ | Build the engine (full C++20)                   |
| CMake         | 3.20+                | Configure and build                             |
| Node.js + npm | 18+                  | Build the web terminal                          |
| Python + pip  | 3.9+                 | Sample data, visualisation, offline ML training |

## Building

### Engine, server, tests and benchmarks

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces four binaries in `build/`:

| Binary            | Purpose                             |
| ----------------- | ----------------------------------- |
| `quantlob`        | Synthetic and LOBSTER CLI simulator |
| `quantlob_server` | REST API and static web host        |
| `quantlob_tests`  | Catch2 unit test runner             |
| `quantlob_bench`  | Google Benchmark harness            |

Tests and benchmarks pull Catch2 and Google Benchmark via CMake FetchContent.
Run the tests with `ctest --output-on-failure` from `build/`.

### CMake options

| Option                      | Default | Description                                    |
| --------------------------- | ------- | ---------------------------------------------- |
| `QUANTLOB_BUILD_TESTS`      | ON      | Catch2 test binary                             |
| `QUANTLOB_BUILD_BENCHMARKS` | ON      | Google Benchmark binary                        |
| `QUANTLOB_BUILD_MAIN`       | ON      | CLI simulator                                  |
| `QUANTLOB_BUILD_SERVER`     | ON      | REST API server                                |
| `QUANTLOB_BUILD_AI_MODELS`  | ON      | ML module (FeatureExtractor, models, pipeline) |
| `QUANTLOB_ENABLE_ASAN`      | OFF     | AddressSanitizer                               |
| `QUANTLOB_ENABLE_TSAN`      | OFF     | ThreadSanitizer                                |
| `QUANTLOB_ENABLE_UBSAN`     | OFF     | UndefinedBehaviorSanitizer                     |
| `QUANTLOB_ENABLE_LTO`       | OFF     | Link-time optimisation                         |

### Frontend

```
cd frontend
npm install
npm run build        # outputs to frontend/dist
```

### Run everything

| Goal                 | Command                                                                           | Result                                                                                               |
| -------------------- | --------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| Full stack           | `scripts/shell/run_stack.sh`                                                      | Builds the frontend and server if needed, serves the API and web terminal at `http://localhost:8080` |
| Engine + tests       | `scripts/shell/build.sh && (cd build && ctest)`                                   | Builds every target and runs the test suite                                                          |
| Synthetic run + plot | `scripts/shell/run_synthetic.sh`                                                  | Runs a simulation, exports CSVs, and renders an order-book chart                                     |
| Frontend dev server  | `cd frontend && npm run dev`                                                      | Vite on port 5173, proxying `/api` to the backend                                                    |
| Docker (full stack)  | `docker compose -f infrastructure/docker/docker-compose.fullstack.yml up --build` | Builds and serves the whole stack in a container                                                     |

For frontend hot reload, run the API and the Vite dev server side by side:

```
./build/quantlob_server 8080 frontend/dist &
cd frontend && npm run dev
```

## Web terminal and REST API

The same server process serves the built frontend and the API on one port. The web
terminal has two workspaces: an **Order Book** view that drives a live price-time
priority book (seed liquidity, submit LIMIT/MARKET/IOC/FOK orders, cancel, and watch
the depth ladder, cumulative depth profile, trades and ML signals update against real
fills), and a **Simulation Lab** that runs a synthetic event stream and charts the
mid-price path, imbalance, per-order latency distribution and ML signal series.

All responses are JSON. Errors return an `{ "error": "..." }` body with an
appropriate status code.

| Method | Path                  | Description                                                |
| ------ | --------------------- | ---------------------------------------------------------- |
| GET    | `/api/health`         | Status, version, active symbol, supported order types      |
| GET    | `/api/book?levels=N`  | Live snapshot: depth ladder, metrics, stats, ML signal     |
| GET    | `/api/trades?limit=N` | Recent trades, newest first                                |
| GET    | `/api/stats`          | Cumulative engine statistics                               |
| POST   | `/api/order`          | Submit an order; returns the match result and updated book |
| POST   | `/api/cancel`         | Cancel a resting order by id                               |
| POST   | `/api/seed`           | Populate the live book with a synthetic run                |
| POST   | `/api/reset`          | Clear the live book and stats                              |
| POST   | `/api/simulate`       | Run a synthetic simulation and return analytics            |

### Request bodies

| Endpoint        | Field                                   | Example                                 | Notes                              |
| --------------- | --------------------------------------- | --------------------------------------- | ---------------------------------- |
| `/api/order`    | `side`                                  | `"buy"` or `"sell"`                     |                                    |
|                 | `type`                                  | `"limit"`, `"market"`, `"ioc"`, `"fok"` | Limit orders need a price          |
|                 | `price`                                 | `150.00`                                | Required for LIMIT                 |
|                 | `quantity`                              | `500`                                   | Positive                           |
| `/api/seed`     | `events`                                | `8000`                                  | Synthetic events to generate       |
|                 | `mid` / `tick` / `seed`                 | `150` / `0.01` / `12345`                | Optional generator parameters      |
| `/api/simulate` | `events`                                | `200000`                                | Synthetic events to run            |
|                 | `snap_interval`                         | `2000`                                  | Events between time-series samples |
|                 | `mid` / `tick` / `seed` / `cancel_rate` |                                         | Optional generator parameters      |

The `/api/simulate` response carries the engine stats, a latency summary and
histogram, a microstructure time series (mid, spread, imbalance, depth), an ML
signal series (mid forecast, buy probability, anomaly score), and the final book
snapshot, all ready for charting.

## CLI reference

```
Options:
  --symbol SYM         Instrument symbol (default: AAPL)
  --lobster MSG.csv    Replay a LOBSTER message CSV
  --realtime           Enable real-time replay pacing
  --speed FACTOR       Replay speed multiplier (default: 1.0)
  --events N           Synthetic event count (default: 500000)
  --mid PRICE          Synthetic initial mid price (default: 150.0)
  --tick SIZE          Tick size (default: 0.01)
  --seed N             RNG seed (default: 12345)
  --levels N           Snapshot depth (default: 5)
  --out-dir DIR        Output directory for CSV/text exports
  --export-trades      Export the trade log to CSV
  --export-snapshot    Export the final order book snapshot
  --export-latency     Export per-order latency samples and summary
  --export-timeseries  Export periodic LOB snapshot time-series
  --snap-interval N    Events between time-series snapshots (default: 1000)
  --log-level LEVEL    DEBUG|INFO|WARN|ERROR (default: INFO)
  --help               Show this help
```

## Data feeds

QuantLOB runs on two feed types. The synthetic feed is a Poisson arrival process
with Gaussian price noise, configurable mid, tick, arrival and cancel rates. The
LOBSTER feed replays a message CSV in the standard LOBSTER format.

Generate a synthetic LOBSTER-format file for testing the replay path:

```
python3 scripts/python/generate_sample_data.py --events 10000
# writes code/data/sample/messages.csv
./build/quantlob --lobster code/data/sample/messages.csv --export-snapshot --out-dir out
```

### LOBSTER message format

Comma-separated, no header row:

| Index | Field      | Type   | Notes                                                       |
| ----- | ---------- | ------ | ----------------------------------------------------------- |
| 0     | time       | float  | Seconds since midnight                                      |
| 1     | event_type | int    | 1=new, 2=partial-cancel, 3=delete, 4=exec, 5=hidden, 7=halt |
| 2     | order_id   | uint64 | Exchange-assigned ID                                        |
| 3     | size       | uint64 | Shares                                                      |
| 4     | price      | int    | Integer scaled x10000 (e.g. 1000000 = 100.00)               |
| 5     | direction  | int    | 1=buy, -1=sell                                              |

## Offline ML tools

```
# Run a simulation that exports the time-series and trade logs
./build/quantlob --events 200000 --export-timeseries --export-trades --out-dir out

# Train the predictors and evaluate them
python3 code/ai_models/python/train_mid_price.py   --data out/lob_timeseries.csv --weights out/mid_weights.json
python3 code/ai_models/python/train_order_flow.py  --trades out/trades.csv --snapshot out/lob_timeseries.csv --weights out/flow_weights.json
python3 code/ai_models/python/evaluate_models.py   --data out/lob_timeseries.csv --mid-weights out/mid_weights.json --flow-weights out/flow_weights.json

# Visualise an order book snapshot or latency distribution
python3 scripts/python/visualize_lob.py book    out/AAPL_snapshot.csv --symbol AAPL --out out/book.png
python3 scripts/python/visualize_lob.py latency out/latency.csv --out out/latency.png
```

## Code style

Following the AlphaForge convention, every C++ translation unit declares
`using namespace std;` after its includes and uses unqualified standard names
(`vector`, `optional`, `chrono::nanoseconds`) rather than the `std::` prefix. The
enum string helpers in `Order.hpp` are named `as_string` so they do not hide the
standard numeric `to_string`. The vendored third-party headers under
`code/third_party` are left untouched.

## Limitations and simplifications

| Area             | Simplification                                                                                                                                    |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| Matching         | Strict price-time priority; no hidden or iceberg orders, no fees or rebates                                                                       |
| Synthetic feed   | Poisson arrivals with Gaussian price noise; a model, not real markets                                                                             |
| LOBSTER          | Message-file replay only; full order-book-file reconstruction is not required                                                                     |
| ML models        | Lightweight online linear models plus an EWMA detector; illustrative, not production alpha                                                        |
| Latency          | The simulation measures wall-clock deltas between order callbacks; the engine is single-threaded and a single-core host shows no parallel speedup |
| Real-time pacing | Replay pacing is a best-effort sleep, not a hard real-time guarantee                                                                              |
| Static mount     | The server needs the absolute path to `frontend/dist`; `run_stack.sh` passes it                                                                   |

## Testing and verification

| Area             | What is covered                                                                                          |
| ---------------- | -------------------------------------------------------------------------------------------------------- |
| Order book       | Add, cancel and modify; best bid/ask/mid/spread; imbalance; VWAP; market impact; snapshot depth          |
| Matching         | LIMIT rest and cross, MARKET sweep, IOC remainder cancel, FOK accept and reject, price-time priority     |
| Feed handler     | Synthetic generation counts, LOBSTER parse and replay                                                    |
| Latency recorder | Mean, stddev, min, max, percentile correctness, and merge                                                |
| Memory pool      | Raw allocate/deallocate, construct/destroy lifetime, exhaustion, and the non-trivial-destructor contract |
| Ring buffer      | Push and pop, wrap-around, full and empty states                                                         |
| ML               | Feature dimension, predictor output ranges, anomaly detector, and pipeline integration with the engine   |

All 131 unit tests pass via CTest. The REST API and the full offline ML pipeline
(export, train, evaluate, visualise) were exercised end to end, and the built
frontend is served by the same C++ server that answers the API.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
