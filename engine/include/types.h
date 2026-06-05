#pragma once
#include <compare>
#include <cstdint>
namespace lob {

using OrderId = uint64_t;
using SequenceNumber = uint64_t;

struct Price {
  uint64_t price;

  Price() : price(0) {};
  explicit Price(uint64_t p) : price(p) {};
  auto operator<=>(const Price &) const = default;
};

struct Quantity {
  uint32_t quantity;

  Quantity() : quantity(0) {};
  explicit Quantity(uint32_t q) : quantity(q) {};

  auto operator<=>(const Quantity &) const = default;
  auto operator+(const Quantity &other) const -> Quantity {
    return Quantity(quantity + other.quantity);
  }

  auto operator-(const Quantity &other) const -> Quantity {
    return Quantity(quantity - other.quantity);
  }

  auto operator+=(const Quantity &other) -> Quantity & {
    quantity += other.quantity;
    return *this;
  }

  auto operator-=(const Quantity &other) -> Quantity & {
    quantity -= other.quantity;
    return *this;
  }
};

enum class Side : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t { LIMIT = 0, MARKET = 1 };

struct alignas(32) Order {
  OrderId id;             // 8 bytes
  Price price;            // 8 bytes
  SequenceNumber seq_num; // 8 bytes
  Quantity quantity;      // 4 bytes - denotes remaining quantity for the order
  Side side : 1;          // 1byte;
  OrderType type : 1;     // 1 byte
  char padding[2];        // 2 bytes - padding to make the struct size a
                          // multiple of 32 bytes

  Order()
      : id(0), price(Price(UINT64_MAX)), seq_num(0), quantity(), side(Side::BUY),
        type(OrderType::LIMIT), padding{} {};
  Order(OrderId id, Price price, SequenceNumber seq_num, Quantity quantity,
        Side side)
      : id(id), price(price), seq_num(seq_num), quantity(quantity), side(side),
        type(OrderType::LIMIT) {};
  Order(OrderId id, SequenceNumber seq_num, Quantity quantity, Side side)
      : id(id), price(side == Side::BUY ? UINT64_MAX : 0), seq_num(seq_num),
        quantity(quantity), side(side), type(OrderType::MARKET) {};
};

static_assert(sizeof(Order) == 32, "Order struct must be 32 bytes");

struct alignas(64) FastOrder {
  Order order;
  FastOrder
      *next; // 8 bytes - pointer to the next order in the same price level
  FastOrder
      *prev; // 8 bytes - pointer to the previous order in the same price level
};

static_assert(sizeof(FastOrder) == 64, "FastOrder struct must be 64 bytes");

} // namespace lob
