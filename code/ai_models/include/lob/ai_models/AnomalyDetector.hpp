#pragma once

#include "FeatureExtractor.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lob {
namespace ai_models {

struct AnomalyDetectorConfig {
    double ewma_alpha          = 0.05;
    double threshold_sigma     = 3.5;
    int    min_anomaly_features = 5;
    int    feature_dim         = static_cast<int>(FEATURE_DIM);
};

struct AnomalyResult {
    double anomaly_score;    ///< Average number of sigma deviations
    int    n_flagged;        ///< Number of features beyond threshold
    bool   is_anomaly;       ///< True when n_flagged >= min_anomaly_features
};

/// Unsupervised anomaly detector based on per-feature EWMA and variance.
///
/// For each feature dimension i:
///   mean_i  <- (1 - alpha) * mean_i  + alpha * x_i
///   var_i   <- (1 - alpha) * var_i   + alpha * (x_i - mean_i)^2
///   z_i     = |x_i - mean_i| / sqrt(var_i)
///
/// A state is flagged as anomalous when at least min_anomaly_features features
/// have z_i > threshold_sigma.
class AnomalyDetector {
public:
    explicit AnomalyDetector(AnomalyDetectorConfig cfg = {});

    /// Update the running EWMA statistics with the current feature vector.
    void update(const FeatureVector& fv);

    /// Compute an anomaly score without updating internal statistics.
    [[nodiscard]] AnomalyResult score(const FeatureVector& fv) const;

    bool load_state(const std::string& path);
    bool save_state(const std::string& path) const;
    void reset() noexcept;

    [[nodiscard]] bool     is_warmed_up() const noexcept { return warmed_up_; }
    [[nodiscard]] uint64_t n_updates()    const noexcept { return n_updates_; }

private:
    AnomalyDetectorConfig cfg_;
    std::vector<double>   ewma_mean_;
    std::vector<double>   ewma_var_;
    bool                  warmed_up_{false};
    uint64_t              n_updates_{0};
};

} // namespace ai_models
} // namespace lob
