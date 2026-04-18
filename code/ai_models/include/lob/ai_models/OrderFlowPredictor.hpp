#pragma once

#include "FeatureExtractor.hpp"

#include <string>
#include <vector>

namespace lob {
namespace ai_models {

struct OrderFlowPredictorConfig {
    double learning_rate  = 0.005;
    double regularisation = 0.01;
    int    feature_dim    = static_cast<int>(FEATURE_DIM);
};

/// Online logistic regression for predicting whether the next arriving order
/// will be a BUY (label 1.0) or SELL (label 0.0).
/// Updated via stochastic gradient descent with L2 regularisation.
class OrderFlowPredictor {
public:
    explicit OrderFlowPredictor(OrderFlowPredictorConfig cfg = {});

    /// Returns probability that the next order will be a BUY, in [0, 1].
    [[nodiscard]] double predict(const FeatureVector& fv) const;

    /// @param label  1.0 = was BUY, 0.0 = was SELL
    void update(const FeatureVector& fv, double label);

    bool load_weights(const std::string& path);
    bool save_weights(const std::string& path) const;
    void reset() noexcept;

    [[nodiscard]] uint64_t n_updates() const noexcept { return n_updates_; }

private:
    static double sigmoid(double x) noexcept;

    OrderFlowPredictorConfig cfg_;
    std::vector<double>      weights_;
    double                   bias_;
    uint64_t                 n_updates_;
};

} // namespace ai_models
} // namespace lob
