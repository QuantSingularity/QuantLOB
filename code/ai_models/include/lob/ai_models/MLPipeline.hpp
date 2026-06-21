#pragma once

#include "AnomalyDetector.hpp"
#include "FeatureExtractor.hpp"
#include "MidPricePredictor.hpp"
#include "OrderFlowPredictor.hpp"

#include "lob/Order.hpp"
#include "lob/OrderBook.hpp"

#include <vector>

using namespace std;

namespace lob {
namespace ai_models {

struct PipelineConfig {
    int    feature_window          = 50;
    int    trade_window            = 20;
    bool   enable_mid_predictor   = true;
    bool   enable_flow_predictor  = true;
    bool   enable_anomaly_detector = true;

    MidPricePredictorConfig   mid_cfg{};
    OrderFlowPredictorConfig  flow_cfg{};
    AnomalyDetectorConfig     anomaly_cfg{};
};

struct PipelineResult {
    vector<double> features;          ///< Raw FEATURE_DIM feature vector
    double              mid_price_forecast{0.0}; ///< Predicted mid-price delta
    double              buy_probability{0.5};    ///< P(next order is BUY)
    double              anomaly_score{0.0};
    int                 anomaly_features{0};     ///< Number of flagged features
    bool                is_anomaly{false};
    bool                model_ready{false};      ///< False during warm-up
};

/// Orchestrates FeatureExtractor, MidPricePredictor, OrderFlowPredictor,
/// and AnomalyDetector in a single tick-level update loop.
class MLPipeline {
public:
    MLPipeline();
    explicit MLPipeline(const PipelineConfig& cfg);

    /// Call once per book update tick.  Returns the current predictions.
    PipelineResult update(const BookSnapshot& snap);

    /// Record a trade for order-flow features.  Call from a trade callback.
    void on_trade(const Trade& trade);

    /// Provide a ground-truth label for online learning (call after next tick).
    void on_label(double actual_mid_delta, double actual_direction);

    void reset() noexcept;

    [[nodiscard]] const PipelineConfig& config() const noexcept { return cfg_; }

private:
    PipelineConfig       cfg_;
    FeatureExtractor     extractor_;
    MidPricePredictor    mid_predictor_;
    OrderFlowPredictor   flow_predictor_;
    AnomalyDetector      anomaly_detector_;

    double   prev_mid_{0.0};
    bool     has_prev_mid_{false};
    double   pending_mid_label_{0.0};
    bool     has_pending_label_{false};
};

} // namespace ai_models
} // namespace lob
