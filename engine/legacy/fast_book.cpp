#include "fast_book.hpp"
#include "hierarchical_bitset.hpp"
#include "types.h"
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace lob {

FastBook::FastBook(EventListener *listener, Price min_price, Price max_price,
                   Price tick_price, size_t max_orders)
    : Orderbook(listener), min_price_(min_price), max_price_(max_price),
      tick_price_(tick_price),
      num_levels((max_price.price - min_price.price) / tick_price.price + 1),
      max_orders(max_orders), buy_levels_(num_levels), sell_levels_(num_levels),
      sell_bitset_(num_levels), buy_bitset_(num_levels), pool_(max_orders),
      free_list_() {
  free_list_.reserve(max_orders);
  for (size_t i = 0; i < pool_.size(); ++i) {
    free_list_.push_back(&pool_[i]);
  }

  for (size_t i = 0; i < num_levels; ++i) {
    Price p(min_price_.price + i * tick_price_.price);
    buy_levels_[i].price = p;
    sell_levels_[i].price = p;
  }
}

void FastBook::remove_order(FastOrder *order, PriceLevel &level,
                            HierarchicalBitset &bitset,
                            Quantity quantity_to_remove) {
  // In some way you have to assert that order is actually in the level and
  // bitset is set for that price index

  assert(order->order.quantity >= quantity_to_remove &&
         "You can only remove at most the order's quantity");

  level.total_quantity -= quantity_to_remove;
  order->order.quantity -= quantity_to_remove;

  if (order->order.quantity.quantity != 0)
    return;

  if (order->prev) {
    order->prev->next = order->next;
  } else {
    level.head = order->next; // Removing head
  }

  if (order->next) {
    order->next->prev = order->prev;
  } else {
    level.tail = order->prev; // Removing tail
  }

  if (level.empty()) {
    bitset.reset_bit(price_to_index(order->order.price));
  }

  order_id_to_order_.erase(order->order.id);
  *order = FastOrder(); // Reset order
  num_orders_--;
  free_list_.push_back(order); // Return to free list
}

void FastBook::cancel(OrderId order_id) {
  // Check if order exists
  auto entry = order_id_to_order_.find(order_id);
  if (entry == order_id_to_order_.end()) {
    return; // Order not found
  }

  // Disconnect order from price level linked list

  FastOrder *order = entry->second;

  size_t price_index = price_to_index(order->order.price);

  PriceLevel &level = (order->order.side == Side::BUY)
                          ? buy_levels_[price_index]
                          : sell_levels_[price_index];

  HierarchicalBitset &bitset =
      (order->order.side == Side::BUY) ? buy_bitset_ : sell_bitset_;

  Quantity cancelled_qty = order->order.quantity;
  remove_order(order, level, bitset, order->order.quantity);

  if (listener_) {
    listener_->on_order_canceled(order_id, cancelled_qty, Quantity{0});
  }
}

void FastBook::modify(OrderId order_id, Quantity new_quantity) {
  if (new_quantity.quantity == 0) {
    cancel(order_id);
    return;
  }

  // Check if order exists
  auto entry = order_id_to_order_.find(order_id);
  if (entry == order_id_to_order_.end()) {
    return; // Order not found
  }

  FastOrder *order = entry->second;
  size_t price_index = price_to_index(order->order.price);
  PriceLevel &level = (order->order.side == Side::BUY)
                          ? buy_levels_[price_index]
                          : sell_levels_[price_index];

  if (new_quantity > order->order.quantity)
    level.total_quantity += (new_quantity - order->order.quantity);
  else
    level.total_quantity -= (order->order.quantity - new_quantity);
  order->order.quantity = new_quantity;

  if (listener_) {
    listener_->on_order_modified(order_id, new_quantity);
  }
}

Price FastBook::best_bid() const {
  if (!buy_bitset_.any())
    return Price{0};
  return buy_levels_[buy_bitset_.find_last_set_bit()].price;
}

Price FastBook::best_ask() const {
  if (!sell_bitset_.any())
    return Price{0};
  return sell_levels_[sell_bitset_.find_first_set_bit()].price;
}

Quantity FastBook::total_quantity_at_price(Price price, Side side) const {
  if (price.price < min_price_.price || price > max_price_ ||
      (price.price - min_price_.price) % tick_price_.price != 0)
    return Quantity{0};
  size_t price_index = price_to_index(price);
  const PriceLevel &level = (side == Side::BUY) ? buy_levels_[price_index]
                                                : sell_levels_[price_index];
  return level.total_quantity;
}

bool FastBook::empty() const {
  return !buy_bitset_.any() && !sell_bitset_.any();
}

void FastBook::execute_market_order(Order &order) { match_orders(order); }

void FastBook::match_orders(Order &order) {

  HierarchicalBitset &bitset =
      (order.side == Side::BUY) ? sell_bitset_ : buy_bitset_;

  if (!bitset.any())
    return;

  std::vector<PriceLevel> &price_level_array =
      (order.side == Side::BUY) ? sell_levels_ : buy_levels_;

  while (order.quantity.quantity > 0 && bitset.any()) {

    size_t best_index = (order.side == Side::BUY) ? bitset.find_first_set_bit()
                                                  : bitset.find_last_set_bit();

    PriceLevel &best_level = price_level_array[best_index];

    if (order.type == OrderType::LIMIT &&
        ((order.side == Side::BUY && best_level.price > order.price) ||
         (order.side == Side::SELL && best_level.price < order.price)))
      return;

    FastOrder *current_order = best_level.head;

    while (current_order && order.quantity.quantity > 0) {
      Quantity executed_qty =
          std::min(order.quantity, current_order->order.quantity);
      order.quantity -= executed_qty;

      if (listener_) {
        listener_->on_order_filled(order.id, current_order->order.id,
                                   best_level.price, executed_qty);
      }

      remove_order(current_order, best_level, bitset, executed_qty);
      current_order = best_level.head;
    }
  }
}

void FastBook::add_limit_order(Order &order) {
  if (order.price < min_price_ || order.price > max_price_ ||
      (order.price.price - min_price_.price) % tick_price_.price != 0) {
    if (listener_)
      listener_->on_order_canceled(order.id, order.quantity, Quantity{0});
    return;
  }

  match_orders(order);

  if (order.quantity.quantity > 0) {
    assert(num_orders_ < max_orders && "Order pool exhausted");
    FastOrder *slot = free_list_.back();
    free_list_.pop_back();

    size_t index = price_to_index(order.price);
    PriceLevel &level =
        (order.side == Side::BUY) ? buy_levels_[index] : sell_levels_[index];

    FastOrder *old_tail = level.tail;
    *slot = {.order = order, .next = nullptr, .prev = old_tail};

    if (old_tail != nullptr)
      old_tail->next = slot;
    level.tail = slot;

    if (level.head == nullptr)
      level.head = slot;
    level.total_quantity += order.quantity;
    num_orders_++;

    (order.side == Side::BUY ? buy_bitset_ : sell_bitset_).set_bit(index);
    order_id_to_order_[order.id] = slot;

    if (listener_)
      listener_->on_order_added(order);
  }
}

} // namespace lob
