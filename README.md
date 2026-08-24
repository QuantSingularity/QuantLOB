# QuantLOB

![CI Status](https://img.shields.io/github/actions/workflow/status/quantsingularity/QuantLOB/ci.yml?branch=main&label=CI&logo=github)

## High-Performance Limit Order Book Simulator

QuantLOB is a limit order book simulator with a matching engine written from first principles in modern C++20, exposed through an interactive CLI, a JSON REST API, and a React and TypeScript web terminal. Price-time priority matching, synthetic and LOBSTER feed replay, nanosecond latency profiling, and an online machine learning pipeline are all implemented in C++, not delegated to an external library, and the engine compiles once into two static libraries shared by every interface.

## Table of Contents

- [Overview](#overview)
- [Project Structure](#project-structure)
- [Feature Status](#feature-status)
- [Technology Stack](#technology-stack)
- [Architecture](#architecture)
- [Installation and Setup](#installation-and-setup)
- [Running the Stack](#running-the-stack)
- [API Surface](#api-surface)
- [Testing](#testing)
- [CI/CD Pipeline](#cicd-pipeline)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

## Overview

QuantLOB is built as a portfolio piece for quantitative and low-latency engineering roles. The emphasis is on a correct, well-tested matching core and a clean architecture rather than on breadth of half-finished features, and every simplifying assumption is stated plainly in the Limitations section rather than hidden. All 131 unit tests pass via CTest (verified: 131 `TEST_CASE` macros in the source), and CI runs a genuine four-way build matrix (GCC and Clang, Release and Debug, with AddressSanitizer on the Clang Debug leg) plus a separate UndefinedBehaviorSanitizer job and a live smoke test that starts the REST server and calls its real endpoints.

## Project Structure

```
QuantLOB/
├── code/
│   ├── include/lob/                # Core engine headers
│   │   ├── Order.hpp               # Order, Trade, Side/OrderType/OrderStatus, as_string
│   │   ├── OrderBook.hpp           # Price-time priority book, snapshot, metrics, VWAP
│   │   ├── MatchingEngine.hpp      # LIMIT/MARKET/IOC/FOK matching, stats, callbacks
│   │   ├── FeedHandler.hpp         # Synthetic and LOBSTER feeds
│   │   ├── Latency.hpp             # Latency recorder and scoped timer
│   │   ├── Exporter.hpp            # CSV and text exporters
│   │   ├── Logger.hpp              # Thread-safe singleton logger
│   │   ├── MemoryPool.hpp          # Lock-free fixed-capacity pool
│   │   └── RingBuffer.hpp          # Single-producer/single-consumer ring buffer
│   ├── src/                        # Engine implementation (static lib quantlob_core)
│   ├── server/                     # REST API server (cpp-httplib + nlohmann/json)
│   ├── third_party/                # Vendored httplib and nlohmann/json (left untouched)
│   ├── tests/                      # Catch2 unit tests (core)
│   ├── benchmarks/                 # Google Benchmark suite
│   ├── data/sample/                # LOBSTER CSV directory (generated, not committed)
│   └── ai_models/                  # Machine learning module (static lib quantlob_ml_core)
│       ├── include/lob/ai_models/  # ML public headers
│       ├── src/                    # ML implementation
│       ├── python/                 # Offline training and evaluation scripts
│       └── tests/                  # ML unit tests
├── frontend/                       # React + TypeScript + Vite web terminal
├── infrastructure/
│   ├── cmake/                      # Compiler warning and sanitizer helpers
│   └── docker/                     # Engine image, full-stack image, and compose files
├── scripts/
│   ├── python/                     # Sample data generation and visualisation
│   └── shell/                      # Build, test and full-stack run helpers
├── docs/                           # Eight reference documents (see docs/README.md)
├── CMakeLists.txt
└── README.md
```

## Feature Status

### Application tier (wired and tested)

| Component           | Details                                                                                                                                                                                                                                                        |
| :------------------ | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Order book**      | Price-time priority book with best bid/ask/mid/spread, relative spread, imbalance, per-side VWAP, a market-impact estimate, and top-N snapshots.                                                                                                               |
| **Matching engine** | LIMIT, MARKET, IOC, and FOK crossing with trade, reject, and fill callbacks, plus cumulative engine statistics. Crossing is strict price-time priority: best price first, then FIFO within a price level.                                                      |
| **Feed handler**    | Synthetic Poisson order flow with Gaussian price noise, and LOBSTER message CSV replay with optional real-time pacing.                                                                                                                                         |
| **Latency**         | A per-sample recorder with mean, stddev, min, max, and p50/p90/p99/p99.9, plus an RAII scoped timer.                                                                                                                                                           |
| **ML pipeline**     | A 40-dimension feature extractor, online mid-price and order-flow predictors, and an EWMA anomaly detector, orchestrated per tick. Lightweight online linear models, illustrative rather than production alpha, as the project's own limitations table states. |
| **Exporter**        | Snapshot, trade log, latency samples and summary, engine stats, and time-series CSVs consumed by the Python tools.                                                                                                                                             |
| **Interfaces**      | A CLI simulator, a REST API (cpp-httplib and nlohmann/json), and a live web terminal with a depth ladder and charts, all backed by the same compiled engine.                                                                                                   |

## Technology Stack

| Area               | Technology                                                          |
| :----------------- | :------------------------------------------------------------------ |
| Engine             | C++20, CMake, Ninja                                                 |
| REST server        | cpp-httplib, nlohmann/json (both vendored under `code/third_party`) |
| Testing            | Catch2 (131 unit tests), Google Benchmark                           |
| Sanitizers         | AddressSanitizer, UndefinedBehaviorSanitizer                        |
| Offline ML tooling | Python, used for training weights and visualizing exported CSVs     |
| Web frontend       | React, TypeScript, Vite, Recharts                                   |
| Infrastructure     | Docker, Docker Compose                                              |
| CI/CD              | GitHub Actions                                                      |

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

The engine compiles once into two static libraries (`quantlob_core` and `quantlob_ml_core`). The CLI simulator, the REST server, the test runner, and the benchmark harness all link against those libraries, so there is exactly one implementation of every calculation.

Following the AlphaForge convention, every C++ translation unit declares `using namespace std;` after its includes and uses unqualified standard names (`vector`, `optional`, `chrono::nanoseconds`) rather than the `std::` prefix. The enum string helpers in `Order.hpp` are named `as_string` so they don't hide the standard numeric `to_string`.

See the [documentation set](#documentation) for detail, starting with `docs/01_architecture.md`.

## Installation and Setup

Prerequisites: a C++20 compiler (GCC 12+ or Clang 14+), CMake, and Node.js 20+ for the frontend.

```bash
git clone https://github.com/quantsingularity/QuantLOB.git
cd QuantLOB

# Engine
cmake -B build -DQUANTLOB_BUILD_TESTS=ON -DQUANTLOB_BUILD_BENCHMARKS=ON -DQUANTLOB_BUILD_MAIN=ON
cmake --build build --parallel

# Frontend
cd frontend && npm install && cd ..
```

## Running the Stack

```bash
# CLI simulator, synthetic feed
./build/quantlob --events 500000 --export-trades --out-dir out

# REST server (needs the absolute path to the built frontend for static hosting)
./build/quantlob_server 8123 "$(pwd)/frontend/dist"

# Frontend dev server
cd frontend && npm run dev
```

Or use the helper script for the full stack (engine build, frontend build, and server) in one step: `scripts/shell/run_stack.sh`. See `docs/03_build_and_configuration.md` for CMake options and `infrastructure/docker/` for the containerized setup.

### CLI reference

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

### Data feeds

QuantLOB runs on two feed types. The synthetic feed is a Poisson arrival process with Gaussian price noise, configurable mid, tick, arrival, and cancel rates. The LOBSTER feed replays a message CSV in the standard LOBSTER format (comma-separated, no header: time, event type, order ID, size, price scaled x10000, direction).

```bash
python3 scripts/python/generate_sample_data.py --events 10000
# writes code/data/sample/messages.csv
./build/quantlob --lobster code/data/sample/messages.csv --export-snapshot --out-dir out
```

### Offline ML tools

```bash
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

## API Surface

| Endpoint        | Key fields                                                                    | Notes                                       |
| :-------------- | :---------------------------------------------------------------------------- | :------------------------------------------ |
| `/api/order`    | `type` (`"limit"`, `"market"`, `"ioc"`, `"fok"`), `price`, `quantity`         | Price required for LIMIT orders             |
| `/api/seed`     | `events`, plus optional `mid`, `tick`, `seed`                                 | Generates synthetic events to seed the book |
| `/api/simulate` | `events`, `snap_interval`, plus optional `mid`, `tick`, `seed`, `cancel_rate` | Runs a full synthetic simulation            |

The `/api/simulate` response carries the engine stats, a latency summary and histogram, a microstructure time series (mid, spread, imbalance, depth), an ML signal series (mid forecast, buy probability, anomaly score), and the final book snapshot, all ready for charting. Full schemas are in `docs/02_api_reference.md`.

## Testing

```bash
cd build && ctest --output-on-failure --parallel 4
```

| Area             | What is covered                                                                                      |
| :--------------- | :--------------------------------------------------------------------------------------------------- |
| Order book       | Add, cancel, and modify; best bid/ask/mid/spread; imbalance; VWAP; market impact; snapshot depth     |
| Matching         | LIMIT rest and cross, MARKET sweep, IOC remainder cancel, FOK accept and reject, price-time priority |
| Feed handler     | Synthetic generation counts, LOBSTER parse and replay                                                |
| Latency recorder | Mean, stddev, min, max, percentile correctness, and merge                                            |
| Memory pool      | Raw allocate/deallocate, construct/destroy lifetime, exhaustion, non-trivial-destructor contract     |
| Ring buffer      | Push and pop, wrap-around, full and empty states                                                     |
| ML               | Feature dimension, predictor output ranges, anomaly detector, pipeline integration with the engine   |

All 131 unit tests pass via CTest (Catch2). The REST API and the full offline ML pipeline (export, train, evaluate, visualise) were exercised end to end, and the built frontend is served by the same C++ server that answers the API. See `docs/08_testing_guide.md` for more.

## CI/CD Pipeline

GitHub Actions (`.github/workflows/ci.yml`) runs three jobs on push and pull request:

| Job             | What it does                                                                                                                                                                                                                                                                                         |
| :-------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| build-and-test  | A 4-way matrix (GCC 12 and Clang 14, each Release and Debug, with AddressSanitizer enabled on the Clang Debug leg). Configures with CMake and Ninja, builds, runs the full CTest suite, smoke-tests the CLI binary, and starts the real REST server to smoke-test `/api/health` and `/api/simulate`. |
| sanitizer-check | A separate Clang Debug build with UndefinedBehaviorSanitizer enabled, running the full CTest suite.                                                                                                                                                                                                  |
| frontend        | Installs frontend dependencies, type-checks, and builds with Vite.                                                                                                                                                                                                                                   |

## Documentation

| Document                                                                 | Contents                                          |
| :----------------------------------------------------------------------- | :------------------------------------------------ |
| [docs/README.md](docs/README.md)                                         | Documentation index                               |
| [docs/01_architecture.md](docs/01_architecture.md)                       | System architecture                               |
| [docs/02_api_reference.md](docs/02_api_reference.md)                     | REST API reference                                |
| [docs/03_build_and_configuration.md](docs/03_build_and_configuration.md) | CMake options, build types, configuration         |
| [docs/04_matching_engine.md](docs/04_matching_engine.md)                 | Matching semantics in detail                      |
| [docs/05_ml_module.md](docs/05_ml_module.md)                             | Feature extraction, predictors, anomaly detection |
| [docs/06_data_formats.md](docs/06_data_formats.md)                       | LOBSTER format, exported CSV schemas              |
| [docs/07_performance_guide.md](docs/07_performance_guide.md)             | Latency methodology, benchmarking                 |
| [docs/08_testing_guide.md](docs/08_testing_guide.md)                     | Test suite structure and coverage                 |

## Limitations and Simplifications

| Area             | Simplification                                                                                                                                    |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| Matching         | Strict price-time priority; no hidden or iceberg orders, no fees or rebates                                                                       |
| Synthetic feed   | Poisson arrivals with Gaussian price noise; a model, not real markets                                                                             |
| LOBSTER          | Message-file replay only; full order-book-file reconstruction is not required                                                                     |
| ML models        | Lightweight online linear models plus an EWMA detector; illustrative, not production alpha                                                        |
| Latency          | The simulation measures wall-clock deltas between order callbacks; the engine is single-threaded and a single-core host shows no parallel speedup |
| Real-time pacing | Replay pacing is a best-effort sleep, not a hard real-time guarantee                                                                              |
| Static mount     | The server needs the absolute path to `frontend/dist`; `run_stack.sh` passes it                                                                   |

## Contributing

Open a pull request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
