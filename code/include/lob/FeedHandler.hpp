#pragma once

#include "Order.hpp"
#include "MatchingEngine.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lob {

enum class FeedType { LOBSTER_CSV, SYNTHETIC };

struct FeedConfig {
    FeedType    type            = FeedType::SYNTHETIC;
    std::string symbol          = "AAPL";
    std::string message_file;   ///< LOBSTER message file path
    std::string orderbook_file; ///< LOBSTER order-book file path (optional)
    bool        replay_realtime = false;
    double      replay_speed    = 1.0; ///< >1.0 = faster than real-time
};

/// One decoded LOBSTER message-file row.
struct LOBSTEREvent {
    std::chrono::nanoseconds timestamp;
    int                      event_type; ///< 1=new, 2=partial-cancel, 3=delete, 4=exec, 5=hidden, 7=halt
    uint64_t                 order_id;
    uint64_t                 size;
    double                   price;      ///< already divided by 10000
    int                      direction;  ///< 1=buy, -1=sell
};

struct SyntheticConfig {
    double   mid_price    = 100.0;   ///< initial mid price
    double   tick_size    = 0.01;    ///< minimum price increment
    double   arrival_rate = 1000.0;  ///< orders per second (Poisson λ)
    double   cancel_rate  = 0.4;     ///< probability an event is a cancel
    double   price_std    = 0.05;    ///< std-dev of Gaussian price noise
    uint64_t min_qty      = 1;
    uint64_t max_qty      = 100;
    uint64_t num_events   = 100'000;
    uint32_t seed         = 42;
    int      spread_ticks = 2;       ///< initial half-spread in ticks
};

using EventCallback = std::function<void(const LOBSTEREvent&)>;
using OrderCallback = std::function<void(const Order&)>;

class FeedHandler {
public:
    explicit FeedHandler(MatchingEngine& engine);
    ~FeedHandler() noexcept = default;

    FeedHandler(const FeedHandler&)            = delete;
    FeedHandler& operator=(const FeedHandler&) = delete;

    /// Apply a FeedConfig (registers the symbol with the engine).
    void configure(const FeedConfig& cfg);

    // ------------------------------------------------------------------
    // LOBSTER replay
    // ------------------------------------------------------------------

    /// Parse a LOBSTER message CSV and return decoded events.
    /// Skips malformed lines with a WARN log entry; throws on file open error.
    [[nodiscard]] std::vector<LOBSTEREvent> load_lobster_csv(
        const std::filesystem::path& message_file);

    /// Replay a decoded event sequence against the engine.
    /// Optionally sleeps between events if config_.replay_realtime is set.
    void replay_lobster(const std::vector<LOBSTEREvent>& events,
                        const std::string&               symbol);

    // ------------------------------------------------------------------
    // Synthetic generation
    // ------------------------------------------------------------------

    /// Generate a synthetic LOB event stream driven by a Poisson arrival
    /// process with Gaussian price noise.
    void generate_synthetic(const SyntheticConfig& cfg);

    // ------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------
    void set_event_callback(EventCallback cb);
    void set_order_callback(OrderCallback cb);

    // ------------------------------------------------------------------
    // Counters / reset
    // ------------------------------------------------------------------
    [[nodiscard]] uint64_t events_processed() const noexcept { return events_processed_; }

    /// Reset event counter and order-ID sequence.
    /// Call before a fresh replay/generation run to avoid ID collisions
    /// when the same FeedHandler instance is reused.
    void reset() noexcept {
        events_processed_ = 0;
        next_order_id_    = base_order_id_;
    }

private:
    [[nodiscard]] Order build_order_from_event(const LOBSTEREvent& ev,
                                               const std::string&  symbol) const;

    MatchingEngine& engine_;
    FeedConfig      config_;
    EventCallback   event_cb_;
    OrderCallback   order_cb_;
    uint64_t        events_processed_ = 0;

    // Synthetic order IDs start well above the LOBSTER order-ID range so
    // that LOBSTER replay and synthetic generation can coexist without
    // colliding when interleaved on the same engine instance.
    static constexpr uint64_t base_order_id_ = 1'000'000'000ULL;
    uint64_t next_order_id_ = base_order_id_;
};

} // namespace lob
