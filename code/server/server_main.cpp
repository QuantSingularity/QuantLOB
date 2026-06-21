// QuantLOB REST API server.
//
// Exposes the limit order book engine, matching, latency profiling and the ML
// pipeline over a small JSON HTTP API, and serves the built React frontend as
// static files. Two surfaces are provided:
//
//   * A persistent live book for interactive order entry (seed, order, cancel,
//     reset, book, trades).
//   * A stateless simulation endpoint that runs a synthetic event stream and
//     returns time series, latency distribution and ML signals for charting.
//
// Usage: quantlob_server [port] [frontend_dir]

#include "lob/FeedHandler.hpp"
#include "lob/Latency.hpp"
#include "lob/Logger.hpp"
#include "lob/MatchingEngine.hpp"
#include "lob/OrderBook.hpp"
#include "lob/ai_models/MLPipeline.hpp"

#include "JsonSupport.hpp"

#include <httplib.h>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>

using namespace std;
using namespace lob;

namespace {

// ---------------------------------------------------------------------------
// Live, persistent book state shared across requests.
// ---------------------------------------------------------------------------
struct LiveState {
    MatchingEngine            engine;
    FeedHandler               feed{engine};
    ai_models::MLPipeline     pipeline;
    deque<Trade>              recent_trades;
    ai_models::PipelineResult last_ml;
    string                    symbol = "AAPL";
    uint64_t                  next_order_id = 1;
    size_t                    levels = 10;
    mutex                     mu;

    LiveState() {
        engine.register_symbol(symbol);
        engine.set_trade_callback([this](const Trade& t) {
            recent_trades.push_back(t);
            if (recent_trades.size() > 256) recent_trades.pop_front();
            pipeline.on_trade(t);
        });
    }
};

LiveState g_live;

void add_cors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

void send_json(httplib::Response& res, const json& body, int status = 200) {
    add_cors(res);
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void send_error(httplib::Response& res, const string& msg, int status) {
    send_json(res, json{{"error", msg}}, status);
}

Side parse_side(const string& s) {
    return (s == "sell" || s == "SELL" || s == "S") ? Side::SELL : Side::BUY;
}

OrderType parse_type(const string& s) {
    if (s == "market" || s == "MARKET") return OrderType::MARKET;
    if (s == "ioc" || s == "IOC")       return OrderType::IOC;
    if (s == "fok" || s == "FOK")       return OrderType::FOK;
    return OrderType::LIMIT;
}

chrono::nanoseconds now_ns() {
    return chrono::duration_cast<chrono::nanoseconds>(
        chrono::high_resolution_clock::now().time_since_epoch());
}

// Build the live book view: snapshot + metrics + latest ML + engine stats.
json live_book_view() {
    const OrderBook* book = g_live.engine.get_book(g_live.symbol);
    json j;
    if (book) {
        j = snapshot_to_json(*book, g_live.levels);
    } else {
        j = json{{"symbol", g_live.symbol}, {"bids", json::array()},
                 {"asks", json::array()}};
    }
    j["stats"] = stats_to_json(g_live.engine.stats());
    j["ml"]    = ml_to_json(g_live.last_ml);
    return j;
}

} // namespace

int main(int argc, char** argv) {
    Logger::instance().set_level(LogLevel::WARN);

    int port = 8080;
    if (argc > 1) {
        try { port = stoi(argv[1]); } catch (...) {}
    } else if (const char* env = getenv("QUANTLOB_PORT")) {
        try { port = stoi(env); } catch (...) {}
    }
    const string web_dir = (argc > 2) ? argv[2] : "frontend";

    httplib::Server server;

    server.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
        add_cors(res);
        res.status = 204;
    });

    // -- Metadata -----------------------------------------------------------
    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        scoped_lock lk(g_live.mu);
        send_json(res, json{{"status", "ok"},
                            {"version", "1.2.0"},
                            {"symbol", g_live.symbol},
                            {"order_types", {"limit", "market", "ioc", "fok"}}});
    });

    // -- Live book snapshot -------------------------------------------------
    server.Get("/api/book", [](const httplib::Request& req, httplib::Response& res) {
        scoped_lock lk(g_live.mu);
        if (req.has_param("levels")) {
            try { g_live.levels = static_cast<size_t>(stoi(req.get_param_value("levels"))); }
            catch (...) {}
        }
        send_json(res, live_book_view());
    });

    // -- Recent trades tape -------------------------------------------------
    server.Get("/api/trades", [](const httplib::Request& req, httplib::Response& res) {
        scoped_lock lk(g_live.mu);
        size_t limit = 50;
        if (req.has_param("limit")) {
            try { limit = static_cast<size_t>(stoi(req.get_param_value("limit"))); }
            catch (...) {}
        }
        json arr = json::array();
        const auto& tr = g_live.recent_trades;
        size_t start = tr.size() > limit ? tr.size() - limit : 0;
        for (size_t i = tr.size(); i-- > start;) {
            arr.push_back(trade_to_json(tr[i]));
        }
        send_json(res, json{{"trades", arr}});
    });

    // -- Submit an order to the live book -----------------------------------
    server.Post("/api/order", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { send_error(res, "invalid JSON body", 400); return; }

        const string side_s = body.value("side", "buy");
        const string type_s = body.value("type", "limit");
        const double price   = body.value("price", 0.0);
        const uint64_t qty   = body.value("quantity", uint64_t{0});

        if (qty == 0) { send_error(res, "quantity must be positive", 400); return; }
        const OrderType type = parse_type(type_s);
        if (type == OrderType::LIMIT && price <= 0.0) {
            send_error(res, "limit orders require a positive price", 400);
            return;
        }

        scoped_lock lk(g_live.mu);
        Order order(g_live.next_order_id++, parse_side(side_s), type, price, qty,
                    g_live.symbol, now_ns());
        const MatchResult result = g_live.engine.submit_order(order);

        if (const OrderBook* book = g_live.engine.get_book(g_live.symbol)) {
            g_live.last_ml = g_live.pipeline.update(book->snapshot(g_live.levels));
        }

        json out;
        out["execution"] = match_result_to_json(result);
        out["book"]      = live_book_view();
        send_json(res, out);
    });

    // -- Cancel a resting order ---------------------------------------------
    server.Post("/api/cancel", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { send_error(res, "invalid JSON body", 400); return; }
        const uint64_t id = body.value("order_id", uint64_t{0});

        scoped_lock lk(g_live.mu);
        const bool ok = g_live.engine.cancel_order(g_live.symbol, id);
        send_json(res, json{{"cancelled", ok}, {"book", live_book_view()}});
    });

    // -- Seed the live book with synthetic liquidity ------------------------
    server.Post("/api/seed", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = req.body.empty() ? json::object() : json::parse(req.body); }
        catch (...) { send_error(res, "invalid JSON body", 400); return; }

        SyntheticConfig cfg;
        cfg.num_events   = body.value("events", uint64_t{8000});
        cfg.mid_price    = body.value("mid", 150.0);
        cfg.tick_size    = body.value("tick", 0.01);
        cfg.seed         = body.value("seed", uint32_t{12345});
        cfg.arrival_rate = body.value("arrival_rate", 100000.0);
        cfg.cancel_rate  = body.value("cancel_rate", 0.4);

        scoped_lock lk(g_live.mu);
        g_live.engine.reset_book(g_live.symbol);
        g_live.pipeline.reset();
        g_live.recent_trades.clear();

        FeedConfig fc;
        fc.symbol = g_live.symbol;
        fc.type   = FeedType::SYNTHETIC;
        g_live.feed.configure(fc);
        g_live.feed.reset();

        // Warm the ML pipeline on a periodic cadence during the seed run.
        uint64_t counter = 0;
        g_live.feed.set_order_callback([&](const Order&) {
            if (++counter % 50 == 0) {
                if (const OrderBook* b = g_live.engine.get_book(g_live.symbol)) {
                    g_live.last_ml = g_live.pipeline.update(b->snapshot(g_live.levels));
                }
            }
        });
        g_live.feed.generate_synthetic(cfg);
        g_live.feed.set_order_callback(nullptr);

        send_json(res, json{{"seeded", cfg.num_events}, {"book", live_book_view()}});
    });

    // -- Reset the live book ------------------------------------------------
    server.Post("/api/reset", [](const httplib::Request&, httplib::Response& res) {
        scoped_lock lk(g_live.mu);
        g_live.engine.reset_book(g_live.symbol);
        g_live.engine.reset_stats();
        g_live.pipeline.reset();
        g_live.recent_trades.clear();
        g_live.last_ml = ai_models::PipelineResult{};
        send_json(res, json{{"reset", true}, {"book", live_book_view()}});
    });

    // -- Engine stats -------------------------------------------------------
    server.Get("/api/stats", [](const httplib::Request&, httplib::Response& res) {
        scoped_lock lk(g_live.mu);
        send_json(res, stats_to_json(g_live.engine.stats()));
    });

    // -- Run a synthetic simulation and return analytics --------------------
    server.Post("/api/simulate", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = req.body.empty() ? json::object() : json::parse(req.body); }
        catch (...) { send_error(res, "invalid JSON body", 400); return; }

        const string symbol  = body.value("symbol", string{"AAPL"});
        SyntheticConfig cfg;
        cfg.num_events   = body.value("events", uint64_t{200000});
        cfg.mid_price    = body.value("mid", 150.0);
        cfg.tick_size    = body.value("tick", 0.01);
        cfg.seed         = body.value("seed", uint32_t{12345});
        cfg.arrival_rate = body.value("arrival_rate", 100000.0);
        cfg.cancel_rate  = body.value("cancel_rate", 0.4);
        const size_t   levels = body.value("levels", size_t{10});
        const uint64_t snap_interval = body.value("snap_interval", uint64_t{2000});

        MatchingEngine     engine;
        FeedHandler        feed{engine};
        ai_models::MLPipeline pipeline;
        LatencyRecorder    recorder{cfg.num_events + 1024};
        engine.register_symbol(symbol);

        deque<Trade> trades;
        engine.set_trade_callback([&](const Trade& t) {
            trades.push_back(t);
            if (trades.size() > 512) trades.pop_front();
            pipeline.on_trade(t);
        });

        json ts      = json::array();   // market microstructure time series
        json ml_ts   = json::array();   // ML signal time series
        uint64_t counter = 0;
        auto last_tp = chrono::high_resolution_clock::now();

        FeedConfig fc;
        fc.symbol = symbol;
        fc.type   = FeedType::SYNTHETIC;
        feed.configure(fc);

        feed.set_order_callback([&](const Order&) {
            const auto tp = chrono::high_resolution_clock::now();
            recorder.record(chrono::duration_cast<chrono::nanoseconds>(tp - last_tp));
            last_tp = tp;

            if (++counter % snap_interval == 0) {
                const OrderBook* book = engine.get_book(symbol);
                if (!book) return;
                const auto snap = book->snapshot(levels);
                const auto mid    = book->mid_price();
                const auto spread = book->spread();
                ts.push_back(json{
                    {"event", counter},
                    {"mid", mid ? json(*mid) : json(nullptr)},
                    {"spread", spread ? json(*spread) : json(nullptr)},
                    {"imbalance", book->imbalance()},
                    {"bid_depth", book->bid_depth()},
                    {"ask_depth", book->ask_depth()},
                });
                const auto r = pipeline.update(snap);
                ml_ts.push_back(json{
                    {"event", counter},
                    {"mid_forecast", r.mid_price_forecast},
                    {"buy_probability", r.buy_probability},
                    {"anomaly_score", r.anomaly_score},
                    {"is_anomaly", r.is_anomaly},
                });
            }
        });

        const auto t0 = chrono::high_resolution_clock::now();
        feed.generate_synthetic(cfg);
        const auto t1 = chrono::high_resolution_clock::now();
        const double ms = chrono::duration<double, milli>(t1 - t0).count();

        const auto& stats = engine.stats();
        const OrderBook* book = engine.get_book(symbol);

        // Latency histogram (log-spaced buckets in nanoseconds).
        json hist = json::array();
        if (recorder.count() > 0) {
            const uint64_t lo = recorder.min_ns();
            const uint64_t hi = recorder.max_ns();
            const int nb = 24;
            vector<uint64_t> counts(nb, 0);
            const double span = (hi > lo) ? double(hi - lo) : 1.0;
            for (size_t i = 0; i < recorder.count(); ++i) {
                const uint64_t v = recorder.sample(i);
                int b = int(double(v - lo) / span * (nb - 1));
                if (b < 0) b = 0; if (b >= nb) b = nb - 1;
                counts[size_t(b)]++;
            }
            for (int i = 0; i < nb; ++i) {
                hist.push_back(json{
                    {"ns", lo + uint64_t(span * i / (nb - 1))},
                    {"count", counts[size_t(i)]}});
            }
        }

        json recent = json::array();
        for (size_t i = trades.size(); i-- > (trades.size() > 50 ? trades.size() - 50 : 0);) {
            recent.push_back(trade_to_json(trades[i]));
        }

        json out;
        out["symbol"]        = symbol;
        out["wall_ms"]       = ms;
        out["events"]        = feed.events_processed();
        out["throughput"]    = ms > 0.0 ? uint64_t(stats.orders_processed / (ms / 1000.0)) : 0;
        out["stats"]         = stats_to_json(stats);
        out["latency"]       = latency_to_json(recorder);
        out["latency_hist"]  = hist;
        out["time_series"]   = ts;
        out["ml_series"]     = ml_ts;
        out["recent_trades"] = recent;
        if (book) out["final_book"] = snapshot_to_json(*book, levels);
        send_json(res, out);
    });

    // -- Static frontend ----------------------------------------------------
    const filesystem::path web_path{web_dir};
    if (filesystem::exists(web_path)) {
        server.set_mount_point("/", web_dir);
    }
    server.set_error_handler([&](const httplib::Request& rq, httplib::Response& rs) {
        if (rs.status == 404 && rq.path.rfind("/api", 0) != 0) {
            const filesystem::path index = web_path / "index.html";
            if (filesystem::exists(index)) {
                ifstream in(index, ios::binary);
                string html((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
                rs.status = 200;
                rs.set_content(html, "text/html");
            }
        }
    });

    LOB_INFO("Server", "QuantLOB API listening on http://0.0.0.0:" + to_string(port));
    if (!server.listen("0.0.0.0", port)) {
        LOB_ERROR("Server", "failed to bind port " + to_string(port));
        return 1;
    }
    return 0;
}
