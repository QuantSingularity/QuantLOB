#pragma once

#include <chrono>
#include <cstdint>
#include <string>

using namespace std;

namespace lob {

enum class Side      : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t { LIMIT = 0, MARKET = 1, IOC = 2, FOK = 3 };
enum class OrderStatus : uint8_t {
    ACTIVE,
    PARTIAL,
    FILLED,
    CANCELLED,
    REJECTED
};

/// A single executed trade.
struct Trade {
    uint64_t                 buy_order_id;
    uint64_t                 sell_order_id;
    double                   price;
    uint64_t                 quantity;
    chrono::nanoseconds timestamp;
};

/// A resting or incoming order.
struct Order {
    uint64_t                 id;
    Side                     side;
    OrderType                type;
    OrderStatus              status;
    double                   price;
    uint64_t                 quantity;
    uint64_t                 filled_quantity;
    string              symbol;
    chrono::nanoseconds timestamp;

    Order() = default;

    Order(uint64_t                 id_,
          Side                     side_,
          OrderType                type_,
          double                   price_,
          uint64_t                 quantity_,
          string              symbol_,
          chrono::nanoseconds ts_)
        : id(id_),
          side(side_),
          type(type_),
          status(OrderStatus::ACTIVE),
          price(price_),
          quantity(quantity_),
          filled_quantity(0),
          symbol(move(symbol_)),
          timestamp(ts_) {}

    [[nodiscard]] uint64_t remaining() const noexcept {
        return quantity - filled_quantity;
    }

    [[nodiscard]] bool is_active() const noexcept {
        return status == OrderStatus::ACTIVE || status == OrderStatus::PARTIAL;
    }

    [[nodiscard]] bool is_buy()  const noexcept { return side == Side::BUY; }
    [[nodiscard]] bool is_sell() const noexcept { return side == Side::SELL; }

    [[nodiscard]] double fill_ratio() const noexcept {
        if (quantity == 0) return 0.0;
        return static_cast<double>(filled_quantity) /
               static_cast<double>(quantity);
    }
};

/// Human-readable string helpers (useful in logs / tests)
inline const char* as_string(Side s) noexcept {
    return s == Side::BUY ? "BUY" : "SELL";
}

inline const char* as_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::LIMIT:  return "LIMIT";
        case OrderType::MARKET: return "MARKET";
        case OrderType::IOC:    return "IOC";
        case OrderType::FOK:    return "FOK";
    }
    return "UNKNOWN";
}

inline const char* as_string(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::ACTIVE:    return "ACTIVE";
        case OrderStatus::PARTIAL:   return "PARTIAL";
        case OrderStatus::FILLED:    return "FILLED";
        case OrderStatus::CANCELLED: return "CANCELLED";
        case OrderStatus::REJECTED:  return "REJECTED";
    }
    return "UNKNOWN";
}

} // namespace lob
