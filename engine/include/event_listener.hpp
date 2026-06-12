#pragma once
#include "types.h"
#include <variant>

namespace lob {

struct OrderAddedEvent {
  Order order;
};

struct OrderCanceledEvent {
  OrderId  order_id;
  Quantity cancelled_qty;
  Quantity remaining_qty;
};

struct OrderModifiedEvent {
  OrderId order_id;
  Quantity new_quantity;
};

struct FillEvent {
  OrderId aggressor;
  OrderId passive;
  Price price;
  Quantity quantity;
};

using Event = std::variant<OrderAddedEvent, OrderCanceledEvent, OrderModifiedEvent, FillEvent>;

class EventListener {
public:
  virtual ~EventListener() = default;
  virtual void on_order_added(const Order &order) = 0;
  virtual void on_order_canceled(OrderId order_id, Quantity cancelled_qty, Quantity remaining_qty) = 0;
  virtual void on_order_modified(OrderId order_id, Quantity new_quantity) = 0;
  virtual void on_order_filled(OrderId aggressor, OrderId passive, Price price,
                               Quantity quantity) = 0;
};

} // namespace lob
