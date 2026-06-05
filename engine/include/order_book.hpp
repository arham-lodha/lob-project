#pragma once
#include "event_listener.hpp"
#include "types.h"
#include <cstddef>

namespace lob {

class Orderbook {
protected:
  EventListener *listener_;
  Orderbook() : listener_(nullptr) {}
  Orderbook(EventListener *listener) : listener_(listener) {}
  virtual void execute_market_order(Order &order) = 0;
  virtual void add_limit_order(Order &order) = 0;
  virtual void match_orders(Order &incoming_order) = 0;

public:
  virtual ~Orderbook() = default;

  void add(Order order) {
    if (order.type == OrderType::MARKET) {
      execute_market_order(order);
    } else {
      add_limit_order(order);
    }
  };

  virtual void cancel(OrderId order_id) = 0;
  virtual void modify(OrderId order_id, Quantity new_quantity) = 0;
  virtual Price best_bid() const = 0;
  virtual Price best_ask() const = 0;
  virtual Quantity total_quantity_at_price(Price price, Side side) const = 0;

  virtual bool empty() const = 0;
};

} // namespace lob
