#include "lob/ai_models/MidPricePredictor.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>

namespace lob {
namespace ai_models {

MidPricePredictor::MidPricePredictor(MidPricePredictorConfig cfg)
    : cfg_(cfg),
      weights_(static_cast<std::size_t>(cfg.feature_dim), 0.0),
      bias_(0.0),
      n_updates_(0) {}

double MidPricePredictor::predict(const FeatureVector& fv) const {
    if (!fv.valid) return 0.0;
    double y = bias_;
    for (std::size_t i = 0; i < weights_.size() && i < fv.data.size(); ++i)
        y += weights_[i] * fv.data[i];
    // Guard against NaN/Inf produced by corrupt feature vectors
    return std::isfinite(y) ? y : 0.0;
}

void MidPricePredictor::update(const FeatureVector& fv, double actual_delta) {
    if (!fv.valid) return;

    double y_pred = predict(fv);
    double err    = y_pred - actual_delta;
    if (!std::isfinite(err)) return;   // skip corrupt samples

    double lr  = cfg_.learning_rate;
    double lam = cfg_.regularisation;

    // Gradient clipping: bound the error used in the weight update so that
    // a single outlier sample cannot cause unbounded weight growth.
    static constexpr double MAX_GRAD_NORM = 1.0;
    double err_clipped = std::max(-MAX_GRAD_NORM, std::min(MAX_GRAD_NORM, err));

    for (std::size_t i = 0; i < weights_.size() && i < fv.data.size(); ++i) {
        double g = err_clipped * fv.data[i] + lam * weights_[i];
        weights_[i] -= lr * g;
    }
    bias_ -= lr * err_clipped;
    ++n_updates_;
}

void MidPricePredictor::reset() noexcept {
    std::fill(weights_.begin(), weights_.end(), 0.0);
    bias_      = 0.0;
    n_updates_ = 0;
}

bool MidPricePredictor::save_weights(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "{\n"
      << "  \"model\": \"MidPricePredictor\",\n"
      << "  \"version\": \"1.0\",\n"
      << "  \"feature_dim\": " << cfg_.feature_dim << ",\n"
      << "  \"learning_rate\": " << cfg_.learning_rate << ",\n"
      << "  \"regularisation\": " << cfg_.regularisation << ",\n"
      << "  \"bias\": " << bias_ << ",\n"
      << "  \"n_updates\": " << n_updates_ << ",\n"
      << "  \"weights\": [";
    for (std::size_t i = 0; i < weights_.size(); ++i) {
        if (i) f << ", ";
        f << weights_[i];
    }
    f << "]\n}\n";
    return f.good();
}

bool MidPricePredictor::load_weights(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Extract bias
    auto bias_pos = content.find("\"bias\":");
    if (bias_pos != std::string::npos) {
        std::istringstream ss(content.substr(bias_pos + 7));
        ss >> bias_;
        if (!std::isfinite(bias_)) bias_ = 0.0;
    }

    // Extract weights array
    auto arr_pos = content.find("\"weights\":");
    if (arr_pos == std::string::npos) return false;
    auto open  = content.find('[', arr_pos);
    auto close = content.find(']', open);
    if (open == std::string::npos || close == std::string::npos) return false;

    std::istringstream ss(content.substr(open + 1, close - open - 1));
    std::string token;
    std::size_t idx = 0;
    while (std::getline(ss, token, ',') && idx < weights_.size()) {
        try {
            double v = std::stod(token);
            weights_[idx++] = std::isfinite(v) ? v : 0.0;
        } catch (...) { ++idx; }
    }
    return true;
}

} // namespace ai_models
} // namespace lob
