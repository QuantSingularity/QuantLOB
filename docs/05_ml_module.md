# Machine Learning Module

## Table of Contents

1. [Overview](#overview)
2. [Design Principles](#design-principles)
3. [Feature Extraction](#feature-extraction)
4. [Models](#models)
5. [MLPipeline](#mlpipeline)
6. [Python Training Scripts](#python-training-scripts)
7. [Integration with the Engine](#integration-with-the-engine)
8. [Model Performance Benchmarks](#model-performance-benchmarks)
9. [Extending the Module](#extending-the-module)

---

## Overview

The `code/ai_models/` directory adds an online machine learning layer on top of the
core LOB engine. All models are implemented in pure C++20 with no external ML
framework dependency. They are designed to run in the same thread as the
matching engine with microsecond inference latency.

Three complementary models are provided:

| Model              | Task                                         | Algorithm                                                | Output                                       |
| ------------------ | -------------------------------------------- | -------------------------------------------------------- | -------------------------------------------- |
| MidPricePredictor  | Regression: predict next mid-price change    | Online ridge regression with stochastic gradient descent | Predicted price delta (double)               |
| OrderFlowPredictor | Classification: predict next order direction | Online logistic regression with SGD                      | Probability of BUY (double in [0,1])         |
| AnomalyDetector    | Unsupervised: flag abnormal LOB states       | Z-score and exponentially weighted moving average (EWMA) | Anomaly score (double >= 0) and boolean flag |

---

## Design Principles

| Principle                  | Implementation                                                 |
| -------------------------- | -------------------------------------------------------------- |
| Zero external dependencies | All algorithms in header + .cpp; no Eigen, no TensorFlow       |
| Online learning            | Models update on every tick; no batch retraining required      |
| Bounded memory             | Fixed-size feature window; no unbounded growth                 |
| Deterministic inference    | Given the same feature vector, inference is pure arithmetic    |
| Hot-path safe              | No heap allocation during predict(); all buffers pre-allocated |
| Offline training support   | Python scripts export weights that can be loaded at startup    |

---

## Feature Extraction

`FeatureExtractor` converts a `BookSnapshot` plus recent trade history into a
fixed-length feature vector used by all three models.

### Feature vector layout

| Index  | Feature name       | Formula                                              | Description                                |
| ------ | ------------------ | ---------------------------------------------------- | ------------------------------------------ |
| 0      | bid_ask_spread     | ask1 - bid1                                          | Absolute spread in price units             |
| 1      | relative_spread    | (ask1 - bid1) / mid                                  | Spread normalised by mid-price             |
| 2      | mid_price_change   | mid*t - mid*{t-1}                                    | First difference of mid                    |
| 3      | log_return         | log(mid*t / mid*{t-1})                               | Log return                                 |
| 4      | imbalance_l1       | (bid_qty1 - ask_qty1) / (bid_qty1 + ask_qty1)        | Level-1 order book imbalance               |
| 5      | imbalance_l2       | same over levels 1-2                                 | Two-level imbalance                        |
| 6      | imbalance_l3       | same over levels 1-3                                 | Three-level imbalance                      |
| 7      | bid_depth_fraction | total_bid / (total_bid + total_ask)                  | Bid fraction of total depth, in [0, 1]     |
| 8      | ask_depth_fraction | total_ask / (total_bid + total_ask)                  | Ask fraction of total depth, in [0, 1]     |
| 9      | depth_imbalance    | (bid_depth - ask_depth) / total_depth                | Signed depth imbalance, in [-1, 1]         |
| 10     | bid_vwap_deviation | (bid_vwap - mid) / mid                               | Bid VWAP deviation from mid, bounded small |
| 11     | ask_vwap_deviation | (ask_vwap - mid) / mid                               | Ask VWAP deviation from mid, bounded small |
| 12     | trade_flow_norm    | signed_flow / (trade_window \* 1000), clamped [-1,1] | Normalised signed order flow               |
| 13     | trade_intensity    | trade_count / trade_window                           | Fractional window utilisation, in [0, 1]   |
| 14     | price_momentum     | log(mid_last / mid_first)                            | Log return over full window                |
| 15     | volatility         | rolling std of log-returns                           | Short-term volatility estimate             |
| 16--21 | bid_qty_l1..l6     | bid quantities at levels 1-6                         | Per-level bid depth                        |
| 22--27 | ask_qty_l1..l6     | ask quantities at levels 1-6                         | Per-level ask depth                        |
| 28--33 | bid_price_l1..l6   | bid prices at levels 1-6                             | Normalised by mid                          |
| 34--39 | ask_price_l1..l6   | ask prices at levels 1-6                             | Normalised by mid                          |

Total: 40 features.

### Normalisation

All quantity features are normalised by the mean depth across the window.
All price features are expressed relative to the current mid-price.
This ensures the feature vector is scale-invariant across instruments.

---

## Models

### MidPricePredictor

Predicts the signed change in mid-price at the next tick using online ridge regression.

| Parameter        | Default | Description                             |
| ---------------- | ------- | --------------------------------------- |
| `learning_rate`  | 0.001   | SGD step size                           |
| `regularisation` | 0.01    | L2 ridge penalty                        |
| `horizon`        | 1       | Ticks ahead to predict                  |
| `feature_dim`    | 40      | Must match FeatureExtractor output size |

```cpp
MidPricePredictor pred;
pred.update(features, actual_delta);   // online weight update
double forecast = pred.predict(features);  // predicted mid-price change
```

### OrderFlowPredictor

Predicts whether the next arriving order will be a BUY or SELL using online logistic regression.

| Parameter        | Default | Description                             |
| ---------------- | ------- | --------------------------------------- |
| `learning_rate`  | 0.005   | SGD step size                           |
| `regularisation` | 0.01    | L2 penalty                              |
| `feature_dim`    | 40      | Must match FeatureExtractor output size |

```cpp
OrderFlowPredictor ofp;
ofp.update(features, was_buy ? 1.0 : 0.0);
double buy_prob = ofp.predict(features);   // probability of next order being BUY
```

### AnomalyDetector

Flags abnormal LOB states by tracking the per-feature EWMA and variance.
A state is flagged as anomalous when more than `threshold_sigma` features
deviate from their EWMA by more than `threshold_sigma` standard deviations.

| Parameter              | Default | Description                                            |
| ---------------------- | ------- | ------------------------------------------------------ |
| `ewma_alpha`           | 0.05    | EWMA smoothing factor                                  |
| `threshold_sigma`      | 3.5     | Z-score threshold for a single feature                 |
| `min_anomaly_features` | 5       | Minimum number of anomalous features to trigger a flag |

```cpp
AnomalyDetector det;
det.update(features);
AnomalyResult r = det.score(features);
if (r.is_anomaly) { /* handle */ }
double s = r.anomaly_score;   // higher = more unusual
```

---

## MLPipeline

`MLPipeline` wires together `FeatureExtractor`, `MidPricePredictor`,
`OrderFlowPredictor`, and `AnomalyDetector` in a single class with a clean interface.

```cpp
lob::ai_models::MLPipeline pipeline;
pipeline.configure(lob::ai_models::PipelineConfig{});

// Attach to the matching engine
engine.set_trade_callback([&](const lob::Trade& t) {
    pipeline.on_trade(t);
});

// Call after each book update
const lob::OrderBook* book = engine.get_book("AAPL");
if (book) {
    auto snap = book->snapshot(6);
    auto result = pipeline.update(snap);
    // result.mid_price_forecast, result.buy_probability, result.anomaly_score
}
```

### PipelineConfig fields

| Field                     | Type     | Default | Description                               |
| ------------------------- | -------- | ------- | ----------------------------------------- |
| `feature_window`          | `int`    | 50      | Ticks of history used by FeatureExtractor |
| `trade_window`            | `int`    | 20      | Recent trades kept for flow features      |
| `enable_mid_predictor`    | `bool`   | true    | Toggle MidPricePredictor                  |
| `enable_flow_predictor`   | `bool`   | true    | Toggle OrderFlowPredictor                 |
| `enable_anomaly_detector` | `bool`   | true    | Toggle AnomalyDetector                    |
| `mid_learning_rate`       | `double` | 0.001   | Forwarded to MidPricePredictor            |
| `flow_learning_rate`      | `double` | 0.005   | Forwarded to OrderFlowPredictor           |
| `anomaly_alpha`           | `double` | 0.05    | Forwarded to AnomalyDetector              |
| `anomaly_threshold`       | `double` | 3.5     | Z-score threshold                         |

### PipelineResult fields

| Field                | Type             | Description                                      |
| -------------------- | ---------------- | ------------------------------------------------ |
| `features`           | `vector<double>` | Raw 40-element feature vector                    |
| `mid_price_forecast` | `double`         | Predicted mid-price delta (ticks)                |
| `buy_probability`    | `double`         | Probability next order is BUY                    |
| `anomaly_score`      | `double`         | EWMA-based anomaly score                         |
| `is_anomaly`         | `bool`           | True if anomaly_score exceeds threshold          |
| `model_ready`        | `bool`           | False during warmup (first feature_window ticks) |

---

## Python Training Scripts

Located in `code/ai_models/python/`.

### train_mid_price.py

Trains a mid-price predictor offline on a CSV file of LOB snapshots and exports
weights to a JSON file loadable by `MidPricePredictor::load_weights()`.

```bash
python3 code/ai_models/python/train_mid_price.py \
    --data out/lob_timeseries.csv \
    --weights out/mid_price_weights.json \
    --epochs 20 \
    --lr 0.001
```

### train_order_flow.py

Trains an order-flow classifier on a trade log CSV.

```bash
python3 code/ai_models/python/train_order_flow.py \
    --trades out/trades.csv \
    --snapshot out/lob_timeseries.csv \
    --weights out/order_flow_weights.json
```

### evaluate_models.py

Loads exported weights, runs inference on held-out data, and prints accuracy metrics.

```bash
python3 code/ai_models/python/evaluate_models.py \
    --data out/lob_timeseries.csv \
    --mid-weights out/mid_price_weights.json \
    --flow-weights out/order_flow_weights.json
```

### Output metrics table (example)

| Model              | MAE    | RMSE   | Directional accuracy | Notes                   |
| ------------------ | ------ | ------ | -------------------- | ----------------------- |
| MidPricePredictor  | 0.0031 | 0.0058 | 54.2%                | 1-tick forecast horizon |
| OrderFlowPredictor | n/a    | n/a    | 57.8%                | Binary classification   |

---

## Integration with the Engine

Recommended usage pattern in a simulation loop:

```cpp
lob::MatchingEngine engine;
lob::FeedHandler    feed{engine};
lob::ai_models::MLPipeline pipeline;

engine.set_trade_callback([&](const lob::Trade& t) {
    pipeline.on_trade(t);
});

feed.set_order_callback([&](const lob::Order& o) {
    const auto* book = engine.get_book(o.symbol);
    if (!book) return;
    auto snap   = book->snapshot(6);
    auto result = pipeline.update(snap);
    if (result.model_ready && result.is_anomaly)
        LOB_WARN("ML", "Anomaly detected, score=" +
                 std::to_string(result.anomaly_score));
});
```

---

## Model Performance Benchmarks

Measured on a single core at 3.8 GHz (Release build, -O3 -march=native).

| Operation                       | Latency (ns) | Notes                      |
| ------------------------------- | ------------ | -------------------------- |
| FeatureExtractor::extract       | 180-320      | Depends on snapshot depth  |
| MidPricePredictor::predict      | 40-80        | Pure dot product           |
| MidPricePredictor::update       | 60-120       | SGD weight update          |
| OrderFlowPredictor::predict     | 50-90        | Logistic sigmoid           |
| OrderFlowPredictor::update      | 70-130       | SGD weight update          |
| AnomalyDetector::score          | 90-160       | 40 Z-score computations    |
| AnomalyDetector::update         | 80-140       | 40 EWMA updates            |
| MLPipeline::update (all models) | 450-900      | Extract + all three models |

These latencies are well within the microsecond budget required for tick-by-tick
processing at typical market data rates (10,000--100,000 events per second).

---

## Extending the Module

To add a new model:

1. Create `code/ai_models/include/lob/ai_models/MyModel.hpp` and `code/ai_models/src/MyModel.cpp`.
2. Declare your model with a `predict(const std::vector<double>& features)` method
   and an `update(const std::vector<double>& features, double label)` method.
3. Add the `.cpp` to the `quantlob_ml_core` target in `CMakeLists.txt`.
4. Add a field to `PipelineResult` and wire it into `MLPipeline::update()`.
5. Write tests in `code/ai_models/tests/test_ml.cpp`.

| Interface requirement                     | Why                                                |
| ----------------------------------------- | -------------------------------------------------- |
| `predict` takes a `const vector<double>&` | Matches FeatureExtractor output type               |
| `update` returns `void`                   | Online; caller does not need to track return value |
| No heap allocation in `predict`           | Hot-path safe                                      |
| No static mutable state                   | Multiple MLPipeline instances must be independent  |
