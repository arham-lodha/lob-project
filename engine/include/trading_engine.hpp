#pragma once
#include "event_listener.hpp"
#include "types.h"
#include <vector>

namespace lob {

template <typename BookImpl> class TradingEngine : public EventListener {
private:
  BookImpl orderbook_;
  std::vector<Event> event_log_;

public:
  TradingEngine() : orderbook_(this) {}

  void add_order(Order &order) { orderbook_.add(order); }
  void cancel_order(OrderId order_id) { orderbook_.cancel(order_id); }
  void modify_order(OrderId order_id, Quantity new_quantity) {
    orderbook_.modify(order_id, new_quantity);
  }

  void on_order_added(const Order &order) override {
    event_log_.push_back(OrderAddedEvent{order});
  }
  void on_order_canceled(OrderId order_id) override {
    event_log_.push_back(OrderCanceledEvent{order_id});
  }
  void on_order_modified(OrderId order_id, Quantity new_quantity) override {
    event_log_.push_back(OrderModifiedEvent{order_id, new_quantity});
  }
  void on_order_filled(OrderId aggressor, OrderId passive, Price price,
                       Quantity quantity) override {
    event_log_.push_back(FillEvent{aggressor, passive, price, quantity});
  }

  const std::vector<Event> &event_log() const { return event_log_; }
};
} // namespace lob
