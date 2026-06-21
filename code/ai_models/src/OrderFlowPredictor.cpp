#include "lob/ai_models/OrderFlowPredictor.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace std;

namespace lob {
namespace ai_models {

OrderFlowPredictor::OrderFlowPredictor(OrderFlowPredictorConfig cfg)
    : cfg_(cfg),
      weights_(static_cast<size_t>(cfg.feature_dim), 0.0),
      bias_(0.0),
      n_updates_(0) {}

double OrderFlowPredictor::sigmoid(double x) noexcept {
    // Numerically stable sigmoid avoiding overflow for large |x|.
    if (x >= 0.0) return 1.0 / (1.0 + exp(-x));
    double ex = exp(x);
    return ex / (1.0 + ex);
}

double OrderFlowPredictor::predict(const FeatureVector& fv) const {
    if (!fv.valid) return 0.5;
    double logit = bias_;
    for (size_t i = 0; i < weights_.size() && i < fv.data.size(); ++i)
        logit += weights_[i] * fv.data[i];
    if (!isfinite(logit)) return 0.5;
    return sigmoid(logit);
}

void OrderFlowPredictor::update(const FeatureVector& fv, double label) {
    if (!fv.valid) return;

    double p   = predict(fv);
    double err = p - label;    // gradient of binary cross-entropy w.r.t. logit
    if (!isfinite(err)) return;

    double lr  = cfg_.learning_rate;
    double lam = cfg_.regularisation;

    // Gradient clipping
    static constexpr double MAX_GRAD_NORM = 1.0;
    double err_clipped = max(-MAX_GRAD_NORM, min(MAX_GRAD_NORM, err));

    for (size_t i = 0; i < weights_.size() && i < fv.data.size(); ++i) {
        double g = err_clipped * fv.data[i] + lam * weights_[i];
        weights_[i] -= lr * g;
    }
    bias_ -= lr * err_clipped;
    ++n_updates_;
}

void OrderFlowPredictor::reset() noexcept {
    fill(weights_.begin(), weights_.end(), 0.0);
    bias_      = 0.0;
    n_updates_ = 0;
}

bool OrderFlowPredictor::save_weights(const string& path) const {
    ofstream f(path);
    if (!f.is_open()) return false;

    f << "{\n"
      << "  \"model\": \"OrderFlowPredictor\",\n"
      << "  \"version\": \"1.0\",\n"
      << "  \"feature_dim\": " << cfg_.feature_dim << ",\n"
      << "  \"learning_rate\": " << cfg_.learning_rate << ",\n"
      << "  \"regularisation\": " << cfg_.regularisation << ",\n"
      << "  \"bias\": " << bias_ << ",\n"
      << "  \"n_updates\": " << n_updates_ << ",\n"
      << "  \"weights\": [";
    for (size_t i = 0; i < weights_.size(); ++i) {
        if (i) f << ", ";
        f << weights_[i];
    }
    f << "]\n}\n";
    return f.good();
}

bool OrderFlowPredictor::load_weights(const string& path) {
    ifstream f(path);
    if (!f.is_open()) return false;

    string content((istreambuf_iterator<char>(f)),
                         istreambuf_iterator<char>());

    auto bias_pos = content.find("\"bias\":");
    if (bias_pos != string::npos) {
        istringstream ss(content.substr(bias_pos + 7));
        ss >> bias_;
        if (!isfinite(bias_)) bias_ = 0.0;
    }

    auto arr_pos = content.find("\"weights\":");
    if (arr_pos == string::npos) return false;
    auto open  = content.find('[', arr_pos);
    auto close = content.find(']', open);
    if (open == string::npos || close == string::npos) return false;

    istringstream ss(content.substr(open + 1, close - open - 1));
    string token;
    size_t idx = 0;
    while (getline(ss, token, ',') && idx < weights_.size()) {
        try {
            double v = stod(token);
            weights_[idx++] = isfinite(v) ? v : 0.0;
        } catch (...) { ++idx; }
    }
    return true;
}

} // namespace ai_models
} // namespace lob
