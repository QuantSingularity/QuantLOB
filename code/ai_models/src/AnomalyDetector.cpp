#include "lob/ai_models/AnomalyDetector.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace lob {
namespace ai_models {

AnomalyDetector::AnomalyDetector(AnomalyDetectorConfig cfg)
    : cfg_(cfg),
      ewma_mean_(static_cast<std::size_t>(cfg.feature_dim), 0.0),
      ewma_var_(static_cast<std::size_t>(cfg.feature_dim), 1.0) {}

void AnomalyDetector::update(const FeatureVector& fv) {
    if (!fv.valid) return;

    double alpha = cfg_.ewma_alpha;

    for (std::size_t i = 0; i < ewma_mean_.size() && i < fv.data.size(); ++i) {
        double x    = fv.data[i];
        double diff = x - ewma_mean_[i];
        ewma_mean_[i] += alpha * diff;
        ewma_var_[i]   = (1.0 - alpha) * ewma_var_[i] + alpha * diff * diff;
    }

    ++n_updates_;
    if (n_updates_ >= static_cast<uint64_t>(cfg_.feature_dim * 2))
        warmed_up_ = true;
}

AnomalyResult AnomalyDetector::score(const FeatureVector& fv) const {
    AnomalyResult result{0.0, 0, false};
    if (!fv.valid || !warmed_up_) return result;

    double total_z   = 0.0;
    int    n_flagged = 0;

    for (std::size_t i = 0; i < ewma_mean_.size() && i < fv.data.size(); ++i) {
        double sd = std::sqrt(ewma_var_[i]);
        if (sd < 1e-12) continue; // skip constant features

        double z = std::abs(fv.data[i] - ewma_mean_[i]) / sd;
        total_z += z;
        if (z > cfg_.threshold_sigma) ++n_flagged;
    }

    result.anomaly_score = total_z / static_cast<double>(ewma_mean_.size());
    result.n_flagged     = n_flagged;
    result.is_anomaly    = (n_flagged >= cfg_.min_anomaly_features);
    return result;
}

void AnomalyDetector::reset() noexcept {
    std::fill(ewma_mean_.begin(), ewma_mean_.end(), 0.0);
    std::fill(ewma_var_.begin(),  ewma_var_.end(),  1.0);
    warmed_up_ = false;
    n_updates_ = 0;
}

bool AnomalyDetector::save_state(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    auto write_array = [&](const std::vector<double>& v) {
        f << "[";
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) f << ", ";
            f << v[i];
        }
        f << "]";
    };

    f << "{\n"
      << "  \"model\": \"AnomalyDetector\",\n"
      << "  \"version\": \"1.0\",\n"
      << "  \"feature_dim\": " << cfg_.feature_dim << ",\n"
      << "  \"ewma_alpha\": " << cfg_.ewma_alpha << ",\n"
      << "  \"threshold_sigma\": " << cfg_.threshold_sigma << ",\n"
      << "  \"n_updates\": " << n_updates_ << ",\n"
      << "  \"ewma_mean\": ";
    write_array(ewma_mean_);
    f << ",\n  \"ewma_variance\": ";
    write_array(ewma_var_);
    f << "\n}\n";
    return f.good();
}

bool AnomalyDetector::load_state(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    auto load_array = [&](const std::string& key, std::vector<double>& out) {
        auto pos = content.find("\"" + key + "\":");
        if (pos == std::string::npos) return;
        auto open  = content.find('[', pos);
        auto close = content.find(']', open);
        if (open == std::string::npos || close == std::string::npos) return;
        std::istringstream ss(content.substr(open + 1, close - open - 1));
        std::string token;
        std::size_t idx = 0;
        while (std::getline(ss, token, ',') && idx < out.size()) {
            try { out[idx++] = std::stod(token); } catch (...) {}
        }
    };

    load_array("ewma_mean", ewma_mean_);
    load_array("ewma_variance", ewma_var_);

    auto nu_pos = content.find("\"n_updates\":");
    if (nu_pos != std::string::npos) {
        std::istringstream ss(content.substr(nu_pos + 12));
        ss >> n_updates_;
        if (n_updates_ >= static_cast<uint64_t>(cfg_.feature_dim * 2))
            warmed_up_ = true;
    }
    return true;
}

} // namespace ai_models
} // namespace lob
