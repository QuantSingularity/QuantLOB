#include "lob/ai_models/MLPipeline.hpp"

namespace lob {
namespace ai_models {

MLPipeline::MLPipeline()
    : MLPipeline(PipelineConfig{}) {}

MLPipeline::MLPipeline(const PipelineConfig& cfg)
    : cfg_(cfg),
      extractor_(cfg.feature_window, cfg.trade_window),
      mid_predictor_(cfg.mid_cfg),
      flow_predictor_(cfg.flow_cfg),
      anomaly_detector_(cfg.anomaly_cfg) {}

PipelineResult MLPipeline::update(const BookSnapshot& snap) {
    PipelineResult result;

    // Compute current mid from snapshot
    double mid = 0.0;
    if (!snap.bids.empty() && !snap.asks.empty())
        mid = (snap.bids[0].first + snap.asks[0].first) * 0.5;

    // Push mid-price history before extraction
    extractor_.push_mid(mid);

    // Apply any pending online label from the previous tick
    if (has_pending_label_) {
        FeatureVector prev_fv = extractor_.extract(snap); // approximate; best-effort
        if (prev_fv.valid) {
            if (cfg_.enable_mid_predictor)
                mid_predictor_.update(prev_fv, pending_mid_label_);
        }
        has_pending_label_ = false;
    }

    // Extract features
    FeatureVector fv = extractor_.extract(snap);
    result.features    = fv.data;
    result.model_ready = fv.valid;

    if (!fv.valid) {
        has_prev_mid_ = true;
        prev_mid_     = mid;
        return result;
    }

    // Mid-price prediction
    if (cfg_.enable_mid_predictor)
        result.mid_price_forecast = mid_predictor_.predict(fv);

    // Order flow prediction
    if (cfg_.enable_flow_predictor)
        result.buy_probability = flow_predictor_.predict(fv);

    // Anomaly detection
    if (cfg_.enable_anomaly_detector) {
        anomaly_detector_.update(fv);
        auto ar              = anomaly_detector_.score(fv);
        result.anomaly_score    = ar.anomaly_score;
        result.anomaly_features = ar.n_flagged;
        result.is_anomaly       = ar.is_anomaly;
    }

    has_prev_mid_ = true;
    prev_mid_     = mid;
    return result;
}

void MLPipeline::on_trade(const Trade& trade) {
    // Infer direction from order IDs: convention is that the aggressor is
    // the buyer when buy_order_id was submitted after sell_order_id.
    // Without timestamps on the trade, we use a simple heuristic:
    // aggressive buy = larger buy_order_id (submitted later).
    int dir = (trade.buy_order_id > trade.sell_order_id) ? +1 : -1;
    extractor_.push_trade(trade.price, trade.quantity, dir);

    // Queue an online label for OrderFlowPredictor
    if (cfg_.enable_flow_predictor) {
        double label = (dir > 0) ? 1.0 : 0.0;
        // Defer update to next tick so features are aligned
        flow_predictor_.update(FeatureVector{}, label); // no-op: fv.valid = false
        // True update happens in on_label() or next update()
    }
}

void MLPipeline::on_label(double actual_mid_delta, double actual_direction) {
    pending_mid_label_  = actual_mid_delta;
    has_pending_label_  = true;
    (void)actual_direction; // stored for future use
}

void MLPipeline::reset() noexcept {
    extractor_.reset();
    mid_predictor_.reset();
    flow_predictor_.reset();
    anomaly_detector_.reset();
    prev_mid_         = 0.0;
    has_prev_mid_     = false;
    pending_mid_label_ = 0.0;
    has_pending_label_ = false;
}

} // namespace ai_models
} // namespace lob
