#pragma once

#include "FeatureExtractor.hpp"

#include <string>
#include <vector>

using namespace std;

namespace lob {
namespace ai_models {

struct MidPricePredictorConfig {
    double learning_rate   = 0.001;
    double regularisation  = 0.01;
    int    horizon         = 1;
    int    feature_dim     = static_cast<int>(FEATURE_DIM);
};

/// Online ridge regression for mid-price delta prediction.
/// Trained incrementally via stochastic gradient descent.
class MidPricePredictor {
public:
    explicit MidPricePredictor(MidPricePredictorConfig cfg = {});

    /// Predict the signed change in mid-price given the current features.
    [[nodiscard]] double predict(const FeatureVector& fv) const;

    /// Update weights given the observed label (actual mid-price delta).
    void update(const FeatureVector& fv, double actual_delta);

    /// Load weights from a JSON file exported by train_mid_price.py.
    bool load_weights(const string& path);

    /// Save current weights to a JSON file.
    bool save_weights(const string& path) const;

    /// Zero all weights (reset to untrained state).
    void reset() noexcept;

    [[nodiscard]] uint64_t n_updates() const noexcept { return n_updates_; }
    [[nodiscard]] double   bias()      const noexcept { return bias_; }

private:
    MidPricePredictorConfig cfg_;
    vector<double>     weights_;
    double                  bias_;
    uint64_t                n_updates_;
};

} // namespace ai_models
} // namespace lob
