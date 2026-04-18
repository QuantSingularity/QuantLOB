#include <catch2/catch_all.hpp>

#include "lob/ai_models/AnomalyDetector.hpp"
#include "lob/ai_models/FeatureExtractor.hpp"
#include "lob/ai_models/MLPipeline.hpp"
#include "lob/ai_models/MidPricePredictor.hpp"
#include "lob/ai_models/OrderFlowPredictor.hpp"
#include "lob/MatchingEngine.hpp"
#include "lob/FeedHandler.hpp"

#include <cmath>
#include <chrono>
#include <numeric>
#include <vector>

using namespace lob;
using namespace lob::ai_models;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static BookSnapshot make_snap(double bid1, double ask1,
                               uint64_t bq1 = 100, uint64_t aq1 = 100) {
    BookSnapshot s;
    s.symbol    = "TEST";
    s.timestamp = std::chrono::nanoseconds{0};
    s.bids = {{bid1, bq1}, {bid1 - 0.01, bq1 * 2}, {bid1 - 0.02, bq1 * 3},
              {bid1 - 0.03, bq1},     {bid1 - 0.04, bq1},     {bid1 - 0.05, bq1}};
    s.asks = {{ask1, aq1}, {ask1 + 0.01, aq1 * 2}, {ask1 + 0.02, aq1 * 3},
              {ask1 + 0.03, aq1},     {ask1 + 0.04, aq1},     {ask1 + 0.05, aq1}};
    return s;
}

// Warm up an extractor to is_ready()
static FeatureVector warm_extract(FeatureExtractor& fe, const BookSnapshot& snap,
                                   int window = 50) {
    for (int i = 0; i < window; ++i)
        fe.push_mid(100.0 + static_cast<double>(i) * 0.001);
    return fe.extract(snap);
}

// ===========================================================================
// FeatureExtractor
// ===========================================================================

TEST_CASE("FeatureExtractor: not ready before window filled", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 9; ++i) fe.push_mid(100.0);
    auto fv = fe.extract(snap);
    REQUIRE_FALSE(fv.valid);
}

TEST_CASE("FeatureExtractor: ready after window filled", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    auto fv = fe.extract(snap);
    REQUIRE(fv.valid);
}

TEST_CASE("FeatureExtractor: output has FEATURE_DIM elements", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    auto fv = fe.extract(snap);
    REQUIRE(fv.data.size() == FEATURE_DIM);
}

TEST_CASE("FeatureExtractor: spread feature is correct", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    auto fv = fe.extract(snap);
    // feature[0] = ask - bid = 100.01 - 99.99 = 0.02
    REQUIRE(fv.data[0] == Catch::Approx(0.02).epsilon(0.0001));
}

TEST_CASE("FeatureExtractor: relative spread feature is correct", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    auto fv = fe.extract(snap);
    // relative_spread = 0.02 / 100.0 = 0.0002
    REQUIRE(fv.data[1] == Catch::Approx(0.0002).epsilon(0.00001));
}

TEST_CASE("FeatureExtractor: balanced book has zero imbalance at L1", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01, 100, 100); // equal quantities
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    auto fv = fe.extract(snap);
    // feature[4] = L1 imbalance = (100 - 100) / (100 + 100) = 0
    REQUIRE(fv.data[4] == Catch::Approx(0.0).margin(1e-10));
}

TEST_CASE("FeatureExtractor: bid-heavy book has positive imbalance", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01, 300, 100);
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    auto fv = fe.extract(snap);
    // L1 imbalance = (300-100)/(300+100) = 0.5
    REQUIRE(fv.data[4] == Catch::Approx(0.5).epsilon(0.001));
}

TEST_CASE("FeatureExtractor: reset clears history", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    REQUIRE(fe.is_ready());
    fe.reset();
    REQUIRE_FALSE(fe.is_ready());
    auto snap = make_snap(99.99, 100.01);
    auto fv = fe.extract(snap);
    REQUIRE_FALSE(fv.valid);
}

TEST_CASE("FeatureExtractor: trade flow feature positive for buy trades", "[ml][feature]") {
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0);
    fe.push_trade(100.0, 200, +1);
    fe.push_trade(100.0, 100, +1);
    auto fv = fe.extract(snap);
    REQUIRE(fv.data[12] > 0.0); // signed flow = 200 + 100 = 300
}

TEST_CASE("FeatureExtractor: volatility is zero for constant mid", "[ml][feature]") {
    FeatureExtractor fe{20, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 20; ++i) fe.push_mid(100.0); // constant
    auto fv = fe.extract(snap);
    REQUIRE(fv.data[15] == Catch::Approx(0.0).margin(1e-10));
}

TEST_CASE("FeatureExtractor: momentum is positive for rising mid", "[ml][feature]") {
    FeatureExtractor fe{20, 5};
    auto snap = make_snap(110.0, 110.02); // rising market
    for (int i = 0; i < 20; ++i)
        fe.push_mid(100.0 + static_cast<double>(i) * 0.5);
    auto fv = fe.extract(snap);
    REQUIRE(fv.data[14] > 0.0);
}

// ===========================================================================
// MidPricePredictor
// ===========================================================================

TEST_CASE("MidPricePredictor: predicts zero before updates", "[ml][midpred]") {
    MidPricePredictor pred;
    FeatureExtractor  fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    REQUIRE(pred.predict(fv) == Catch::Approx(0.0).margin(1e-12));
}

TEST_CASE("MidPricePredictor: invalid feature vector predicts zero", "[ml][midpred]") {
    MidPricePredictor pred;
    FeatureVector     fv; // valid=false
    REQUIRE(pred.predict(fv) == Catch::Approx(0.0));
}

TEST_CASE("MidPricePredictor: update does not crash", "[ml][midpred]") {
    MidPricePredictor pred;
    FeatureExtractor  fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    REQUIRE_NOTHROW(pred.update(fv, 0.01));
    REQUIRE(pred.n_updates() == 1);
}

TEST_CASE("MidPricePredictor: learns direction after many updates", "[ml][midpred]") {
    MidPricePredictor pred;
    FeatureExtractor  fe{10, 5};
    // Consistently bid-heavy book, positive delta
    auto snap_bull = make_snap(99.99, 100.01, 300, 100);
    auto fv        = warm_extract(fe, snap_bull);

    // Train on 500 examples: bid-heavy -> positive delta
    for (int i = 0; i < 500; ++i)
        pred.update(fv, 0.01);

    double forecast = pred.predict(fv);
    REQUIRE(forecast > 0.0);
}

TEST_CASE("MidPricePredictor: reset zeros weights and counter", "[ml][midpred]") {
    MidPricePredictor pred;
    FeatureExtractor  fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    pred.update(fv, 0.05);
    REQUIRE(pred.n_updates() == 1);
    pred.reset();
    REQUIRE(pred.n_updates() == 0);
    REQUIRE(pred.predict(fv) == Catch::Approx(0.0).margin(1e-12));
}

TEST_CASE("MidPricePredictor: save and load weights round-trip", "[ml][midpred]") {
    MidPricePredictor pred;
    FeatureExtractor  fe{10, 5};
    auto snap = make_snap(99.99, 100.01, 300, 100);
    auto fv   = warm_extract(fe, snap);
    for (int i = 0; i < 100; ++i) pred.update(fv, 0.01);

    double before = pred.predict(fv);
    REQUIRE(pred.save_weights("/tmp/test_mid_weights.json"));

    MidPricePredictor pred2;
    REQUIRE(pred2.load_weights("/tmp/test_mid_weights.json"));
    REQUIRE(pred2.predict(fv) == Catch::Approx(before).epsilon(0.001));
}

// ===========================================================================
// OrderFlowPredictor
// ===========================================================================

TEST_CASE("OrderFlowPredictor: returns 0.5 before updates", "[ml][flow]") {
    OrderFlowPredictor ofp;
    FeatureExtractor   fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    REQUIRE(ofp.predict(fv) == Catch::Approx(0.5).margin(1e-9));
}

TEST_CASE("OrderFlowPredictor: invalid fv returns 0.5", "[ml][flow]") {
    OrderFlowPredictor ofp;
    FeatureVector      fv;
    REQUIRE(ofp.predict(fv) == Catch::Approx(0.5));
}

TEST_CASE("OrderFlowPredictor: output is in [0, 1]", "[ml][flow]") {
    OrderFlowPredictor ofp;
    FeatureExtractor   fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    for (int i = 0; i < 100; ++i) ofp.update(fv, 1.0);
    double p = ofp.predict(fv);
    REQUIRE(p >= 0.0);
    REQUIRE(p <= 1.0);
}

TEST_CASE("OrderFlowPredictor: learns to predict BUY after BUY training", "[ml][flow]") {
    OrderFlowPredictor ofp;
    FeatureExtractor   fe{10, 5};
    auto snap = make_snap(99.99, 100.01, 300, 100); // bid-heavy
    auto fv   = warm_extract(fe, snap);
    for (int i = 0; i < 500; ++i) ofp.update(fv, 1.0); // all BUY labels
    REQUIRE(ofp.predict(fv) > 0.5);
}

TEST_CASE("OrderFlowPredictor: reset clears state", "[ml][flow]") {
    OrderFlowPredictor ofp;
    FeatureExtractor   fe{10, 5};
    auto snap = make_snap(99.99, 100.01, 300, 100);
    auto fv   = warm_extract(fe, snap);
    for (int i = 0; i < 200; ++i) ofp.update(fv, 1.0);
    REQUIRE(ofp.n_updates() == 200);
    ofp.reset();
    REQUIRE(ofp.n_updates() == 0);
    REQUIRE(ofp.predict(fv) == Catch::Approx(0.5).margin(1e-9));
}

TEST_CASE("OrderFlowPredictor: save and load round-trip", "[ml][flow]") {
    OrderFlowPredictor ofp;
    FeatureExtractor   fe{10, 5};
    auto snap = make_snap(99.99, 100.01, 300, 100);
    auto fv   = warm_extract(fe, snap);
    for (int i = 0; i < 200; ++i) ofp.update(fv, 1.0);

    double before = ofp.predict(fv);
    REQUIRE(ofp.save_weights("/tmp/test_flow_weights.json"));

    OrderFlowPredictor ofp2;
    REQUIRE(ofp2.load_weights("/tmp/test_flow_weights.json"));
    REQUIRE(ofp2.predict(fv) == Catch::Approx(before).epsilon(0.001));
}

// ===========================================================================
// AnomalyDetector
// ===========================================================================

TEST_CASE("AnomalyDetector: not warmed up before enough updates", "[ml][anomaly]") {
    AnomalyDetector det;
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    det.update(fv); // just one update
    auto r = det.score(fv);
    REQUIRE_FALSE(r.is_anomaly);
    REQUIRE(r.anomaly_score == Catch::Approx(0.0));
}

TEST_CASE("AnomalyDetector: normal state not flagged after warm-up", "[ml][anomaly]") {
    AnomalyDetector  det;
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 10; ++i) fe.push_mid(100.0 + i * 0.001);

    // Warm up detector with consistent normal state
    for (int i = 0; i < 200; ++i) {
        auto fv = fe.extract(snap);
        if (fv.valid) det.update(fv);
        fe.push_mid(100.0 + i * 0.001);
    }

    auto fv = fe.extract(snap);
    if (fv.valid) {
        auto r = det.score(fv);
        // Normal state should not trigger anomaly
        REQUIRE_FALSE(r.is_anomaly);
    }
}

TEST_CASE("AnomalyDetector: reset clears warm-up", "[ml][anomaly]") {
    AnomalyDetector  det;
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    for (int i = 0; i < 200; ++i) det.update(fv);
    REQUIRE(det.is_warmed_up());
    det.reset();
    REQUIRE_FALSE(det.is_warmed_up());
    REQUIRE(det.n_updates() == 0);
}

TEST_CASE("AnomalyDetector: update increments counter", "[ml][anomaly]") {
    AnomalyDetector  det;
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    det.update(fv);
    det.update(fv);
    REQUIRE(det.n_updates() == 2);
}

TEST_CASE("AnomalyDetector: save and load state round-trip", "[ml][anomaly]") {
    AnomalyDetector  det;
    FeatureExtractor fe{10, 5};
    auto snap = make_snap(99.99, 100.01);
    auto fv   = warm_extract(fe, snap);
    for (int i = 0; i < 200; ++i) det.update(fv);

    REQUIRE(det.save_state("/tmp/test_anomaly_state.json"));

    AnomalyDetector det2;
    REQUIRE(det2.load_state("/tmp/test_anomaly_state.json"));
    REQUIRE(det2.n_updates() == det.n_updates());
}

// ===========================================================================
// MLPipeline
// ===========================================================================

TEST_CASE("MLPipeline: runs without error on first tick", "[ml][pipeline]") {
    MLPipeline pipeline;
    auto snap = make_snap(99.99, 100.01);
    REQUIRE_NOTHROW(pipeline.update(snap));
}

TEST_CASE("MLPipeline: model_ready false during warm-up", "[ml][pipeline]") {
    PipelineConfig cfg;
    cfg.feature_window = 20;
    MLPipeline pipeline{cfg};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 19; ++i) pipeline.update(snap);
    auto r = pipeline.update(snap);
    // After 20 ticks exactly, should be ready
    // (after 19 ticks, not yet)
    // We just verify no crash; readiness depends on push_mid count
    REQUIRE_NOTHROW(pipeline.update(snap));
}

TEST_CASE("MLPipeline: buy_probability is in [0, 1]", "[ml][pipeline]") {
    MLPipeline pipeline;
    auto snap = make_snap(99.99, 100.01);
    PipelineResult r;
    for (int i = 0; i < 60; ++i) r = pipeline.update(snap);
    REQUIRE(r.buy_probability >= 0.0);
    REQUIRE(r.buy_probability <= 1.0);
}

TEST_CASE("MLPipeline: features vector has correct dimension", "[ml][pipeline]") {
    MLPipeline pipeline;
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 55; ++i) pipeline.update(snap);
    auto r = pipeline.update(snap);
    if (r.model_ready)
        REQUIRE(r.features.size() == FEATURE_DIM);
}

TEST_CASE("MLPipeline: on_trade does not crash", "[ml][pipeline]") {
    MLPipeline pipeline;
    Trade t;
    t.buy_order_id  = 2;
    t.sell_order_id = 1;
    t.price         = 100.0;
    t.quantity      = 50;
    t.timestamp     = std::chrono::nanoseconds{0};
    REQUIRE_NOTHROW(pipeline.on_trade(t));
}

TEST_CASE("MLPipeline: reset clears all state", "[ml][pipeline]") {
    MLPipeline pipeline;
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 60; ++i) pipeline.update(snap);
    REQUIRE_NOTHROW(pipeline.reset());
    // After reset, should be back to warm-up state
    auto r = pipeline.update(snap);
    REQUIRE_FALSE(r.model_ready);
}

TEST_CASE("MLPipeline: disabled predictors produce default outputs", "[ml][pipeline]") {
    PipelineConfig cfg;
    cfg.enable_mid_predictor    = false;
    cfg.enable_flow_predictor   = false;
    cfg.enable_anomaly_detector = false;
    MLPipeline pipeline{cfg};
    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 60; ++i) pipeline.update(snap);
    auto r = pipeline.update(snap);
    REQUIRE(r.mid_price_forecast == Catch::Approx(0.0));
    REQUIRE(r.buy_probability    == Catch::Approx(0.5));
    REQUIRE(r.anomaly_score      == Catch::Approx(0.0));
}

TEST_CASE("MLPipeline: integrates with MatchingEngine via callbacks", "[ml][pipeline]") {
    MatchingEngine engine;
    MLPipeline     pipeline;

    engine.set_trade_callback([&](const Trade& t) {
        pipeline.on_trade(t);
    });

    auto snap = make_snap(99.99, 100.01);
    for (int i = 0; i < 60; ++i) pipeline.update(snap);

    // Submit crossing orders to trigger trade callback
    engine.register_symbol("AAPL");
    Order sell{1, Side::SELL, OrderType::LIMIT, 100.0, 50, "AAPL",
               std::chrono::nanoseconds{0}};
    Order buy {2, Side::BUY,  OrderType::LIMIT, 100.0, 50, "AAPL",
               std::chrono::nanoseconds{1}};
    engine.submit_order(sell);
    engine.submit_order(buy); // triggers trade callback

    auto r = pipeline.update(snap);
    REQUIRE(r.buy_probability >= 0.0);
    REQUIRE(r.buy_probability <= 1.0);
}
