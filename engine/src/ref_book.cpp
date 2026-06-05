#include "ref_book.hpp"
#include "types.h"

namespace lob {

Price RefBook::best_bid() const {
  if (buy_orders_.empty()) {
    return Price(0);
  }
  return buy_orders_.rbegin()->first;
}

Price RefBook::best_ask() const {
  if (sell_orders_.empty()) {
    return Price(0);
  }
  return sell_orders_.begin()->first;
}

Quantity RefBook::total_quantity_at_price(Price price, Side side) const {
  if (side == Side::BUY) {
    auto it = buy_orders_.find(price);
    if (it != buy_orders_.end()) {
      Quantity total_quantity(0);

      for (const auto &order : it->second) {
        total_quantity += order.quantity;
      }

      return total_quantity;
    }
  } else {
    auto it = sell_orders_.find(price);
    if (it != sell_orders_.end()) {
      Quantity total_quantity(0);

      for (const auto &order : it->second) {
        total_quantity += order.quantity;
      }

      return total_quantity;
    }
  }
  return Quantity(0);
}

void RefBook::execute_market_order(Order &order) {
  if (order.side == Side::BUY) {
    while (!sell_orders_.empty() && order.quantity.quantity > 0) {
      match_orders(order);
    }
  } else {
    while (!buy_orders_.empty() && order.quantity.quantity > 0) {
      match_orders(order);
    }
  }
}

void RefBook::match_orders(Order &incoming_order) {
  if (incoming_order.side == Side::BUY) {
    auto it = sell_orders_.begin();
    while (it != sell_orders_.end() && incoming_order.quantity.quantity > 0 &&
           incoming_order.price >= it->first) {
      auto &orders_at_price = it->second;
      for (auto order_it = orders_at_price.begin();
           order_it != orders_at_price.end() &&
           incoming_order.quantity.quantity > 0;) {
        Quantity fill_quantity =
            std::min(incoming_order.quantity, order_it->quantity);
        Price fill_price = it->first;

        // Update quantities
        incoming_order.quantity -= fill_quantity;
        order_it->quantity -= fill_quantity;

        // Notify listener about the fill
        if (listener_ != nullptr) {
          listener_->on_order_filled(incoming_order.id, order_it->id,
                                     fill_price, fill_quantity);
        }

        // Remove the order if it's fully filled
        if (order_it->quantity.quantity == 0) {
          order_id_to_location_.erase(order_it->id);
          order_it = orders_at_price.erase(order_it);
        } else {
          ++order_it;
        }
      }

      // Remove the price level if there are no more orders
      if (orders_at_price.empty()) {
        it = sell_orders_.erase(it);
      } else {
        ++it;
      }
    }
  } else {
    auto it = buy_orders_.rbegin();
    while (it != buy_orders_.rend() && incoming_order.quantity.quantity > 0 &&
           incoming_order.price <= it->first) {
      auto &orders_at_price = it->second;
      for (auto order_it = orders_at_price.begin();
           order_it != orders_at_price.end() &&
           incoming_order.quantity.quantity > 0;) {
        Quantity fill_quantity =
            std::min(incoming_order.quantity, order_it->quantity);
        Price fill_price = it->first;

        incoming_order.quantity -= fill_quantity;
        order_it->quantity -= fill_quantity;

        if (listener_ != nullptr) {
          listener_->on_order_filled(incoming_order.id, order_it->id,
                                     fill_price, fill_quantity);
        }

        if (order_it->quantity.quantity == 0) {
          order_id_to_location_.erase(order_it->id);
          order_it = orders_at_price.erase(order_it);
        } else {
          ++order_it;
        }
      }

      if (orders_at_price.empty()) {
        it =
            std::make_reverse_iterator(buy_orders_.erase(std::next(it).base()));
      } else {
        ++it;
      }
    }
  }
}

void RefBook::add_limit_order(Order &order) {
  if ((order.side == Side::BUY && !sell_orders_.empty() &&
       order.price >= best_ask()) ||
      (order.side == Side::SELL && !buy_orders_.empty() &&
       order.price <= best_bid())) {
    match_orders(order);
    if (order.quantity.quantity == 0) {
      return;
    }
  }

  if (order.side == Side::BUY) {
    buy_orders_[order.price].push_back(order);
    order_id_to_location_[order.id] = {order.price, order.side};
  } else {
    sell_orders_[order.price].push_back(order);
    order_id_to_location_[order.id] = {order.price, order.side};
  }

  if (listener_ != nullptr) {
    listener_->on_order_added(order);
  }

  return;
}

void RefBook::cancel(OrderId order_id) {
  auto entry = order_id_to_location_.find(order_id);
  if (entry == order_id_to_location_.end()) {
    return;
  }

  OrderLocation location = entry->second;
  std::map<Price, std::vector<Order>> &orders_map =
      (location.side == Side::BUY) ? buy_orders_ : sell_orders_;
  std::vector<Order> &orders_at_price = orders_map[location.price];

  for (auto it = orders_at_price.begin(); it != orders_at_price.end(); ++it) {
    if (it->id == order_id) {
      orders_at_price.erase(it);
      order_id_to_location_.erase(entry);
      if (orders_at_price.empty()) {
        orders_map.erase(location.price);
      }

      if (listener_ != nullptr) {
        listener_->on_order_canceled(order_id);
      }

      return;
    }
  }
}

void RefBook::modify(OrderId order_id, Quantity new_quantity) {
  if (auto it = order_id_to_location_.find(order_id);
      it != order_id_to_location_.end()) {
    OrderLocation location = it->second;
    std::map<Price, std::vector<Order>> &orders_map =
        (location.side == Side::BUY) ? buy_orders_ : sell_orders_;
    std::vector<Order> &orders_at_price = orders_map[location.price];

    for (auto order_it = orders_at_price.begin();
         order_it != orders_at_price.end(); ++order_it) {
      if (order_it->id == order_id) {
        order_it->quantity = new_quantity;
        if (listener_ != nullptr) {
          listener_->on_order_modified(order_id, new_quantity);
        }
        break;
      }
    }
  }
}

bool RefBook::empty() const {
  return buy_orders_.empty() && sell_orders_.empty();
}

} // namespace lob
