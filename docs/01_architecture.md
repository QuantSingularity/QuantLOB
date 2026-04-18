# QuantLOB Architecture Reference

## Table of Contents

1. [Project Overview](#project-overview)
2. [Directory Layout](#directory-layout)
3. [Component Map](#component-map)
4. [Data Flow](#data-flow)
5. [Threading Model](#threading-model)
6. [Build Targets](#build-targets)

---

## Project Overview

QuantLOB is a C++20, header-based limit order book (LOB) engine designed for
high-throughput simulation, LOBSTER market data replay, latency profiling, and
machine learning feature extraction. Every hot-path allocation is avoided: the
matching engine works entirely through in-place mutation of `std::map` price
levels and an `std::unordered_map` of live orders.

---

## Directory Layout

```
QuantLOB/
|-- CMakeLists.txt              Root build file
|-- README.md
|-- LICENSE
|-- code/
|   |-- include/lob/            Public C++ headers
|   |-- src/                    Implementation files
|   |-- tests/                  Catch2 unit tests
|   |-- benchmarks/             Google Benchmark suites
|   |-- data/sample/            LOBSTER CSV data directory
|   `-- ai_models/                     Machine learning module
|       |-- include/lob/ai_models/     ML public headers
|       |-- src/                ML implementation files
|       |-- python/             Python training and inference scripts
|       `-- tests/              ML unit tests
|-- docs/                       This documentation tree
|-- infrastructure/
|   |-- cmake/                  Compiler warning and sanitizer helpers
|   `-- docker/                 Docker build context
`-- scripts/
    |-- python/                 Visualisation and data generation
    `-- shell/                  Build and test shell helpers
```

---

## Component Map

| Component          | Header                    | Source                    | Role                                                  |
| ------------------ | ------------------------- | ------------------------- | ----------------------------------------------------- |
| Order              | Order.hpp                 | (header-only)             | POD types: Order, Trade, Side, OrderType, OrderStatus |
| OrderBook          | OrderBook.hpp             | OrderBook.cpp             | Price-time priority book with analytics               |
| MatchingEngine     | MatchingEngine.hpp        | MatchingEngine.cpp        | LIMIT / MARKET / IOC / FOK dispatch                   |
| FeedHandler        | FeedHandler.hpp           | FeedHandler.cpp           | LOBSTER replay and synthetic generation               |
| LatencyRecorder    | Latency.hpp               | (header-only)             | Nanosecond sample accumulator with percentiles        |
| ScopedTimer        | Latency.hpp               | (header-only)             | RAII high-resolution timer                            |
| Logger             | Logger.hpp                | (header-only)             | Thread-safe singleton logger                          |
| Exporter           | Exporter.hpp              | (header-only)             | CSV and key-value file writers                        |
| MemoryPool         | MemoryPool.hpp            | (header-only)             | Lock-free fixed-capacity object pool                  |
| RingBuffer         | RingBuffer.hpp            | (header-only)             | SPSC lock-free ring buffer                            |
| FeatureExtractor   | ml/FeatureExtractor.hpp   | ml/FeatureExtractor.cpp   | LOB state to feature vector                           |
| MidPricePredictor  | ml/MidPricePredictor.hpp  | ml/MidPricePredictor.cpp  | Online linear regression mid-price forecast           |
| OrderFlowPredictor | ml/OrderFlowPredictor.hpp | ml/OrderFlowPredictor.cpp | Logistic regression order-flow direction              |
| AnomalyDetector    | ml/AnomalyDetector.hpp    | ml/AnomalyDetector.cpp    | Z-score and EWMA anomaly detection                    |
| MLPipeline         | ml/MLPipeline.hpp         | ml/MLPipeline.cpp         | Orchestrates feature extraction and all models        |

---

## Data Flow

```
External data
    |
    v
FeedHandler --[LOBSTEREvent / synthetic Order]--> MatchingEngine
                                                        |
                                            +-----------+-----------+
                                            |           |           |
                                        OrderBook  TradeCallback  FillCallback
                                            |
                              +-------------+-------------+
                              |             |             |
                        Snapshot      Analytics      MLPipeline
                              |                          |
                          Exporter               FeatureExtractor
                                                         |
                                            +-----------+-----------+
                                            |           |           |
                                  MidPricePredictor  OrderFlow  Anomaly
                                            |        Predictor  Detector
                                         Exporter
```

---

## Threading Model

| Scenario                                       | Safe? | Notes                                                 |
| ---------------------------------------------- | ----- | ----------------------------------------------------- |
| Single-thread matching                         | Yes   | Default usage; no locking needed in engine or book    |
| Logger from multiple threads                   | Yes   | Internal mutex protects fprintf                       |
| MemoryPool concurrent alloc/dealloc            | Yes   | Lock-free CAS free-stack (MPMC)                       |
| RingBuffer one producer / one consumer         | Yes   | SPSC; head and tail on separate cache lines           |
| RingBuffer multiple producers or consumers     | No    | Use an external mutex or a different queue            |
| MatchingEngine from two threads simultaneously | No    | No internal locking; wrap in a mutex at the call site |
| MLPipeline::update from multiple threads       | No    | Models hold mutable state; synchronise externally     |

---

## Build Targets

| CMake target     | Binary                | Description                                              |
| ---------------- | --------------------- | -------------------------------------------------------- |
| quantlob_core    | libquantlob_core.a    | Static library: OrderBook + MatchingEngine + FeedHandler |
| quantlob_ml_core | libquantlob_ml_core.a | Static library: all ML components                        |
| quantlob_main    | quantlob              | CLI driver with all features                             |
| quantlob_tests   | quantlob_tests        | Catch2 unit tests (core + ML)                            |
| quantlob_bench   | quantlob_bench        | Google Benchmark suite                                   |
