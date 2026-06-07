#include "fast_book.hpp"
#include "hierarchical_bitset.hpp"
#include "types.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace lob {

FastBook::FastBook(EventListener *listener, Price min_price, Price max_price,
                   Price tick_price, size_t max_orders)
    : Orderbook(listener), min_price_(min_price), max_price_(max_price),
      tick_price_(tick_price),
      num_levels((max_price.price - min_price.price) / tick_price.price + 1),
      max_orders(max_orders), buy_levels_(num_levels), sell_levels_(num_levels),
      ht_shift_(0), sell_bitset_(num_levels), buy_bitset_(num_levels),
      pool_(max_orders), free_list_() {
  free_list_.reserve(max_orders);
  for (size_t i = 0; i < pool_.size(); ++i) {
    free_list_.push_back(i);
  }

  for (size_t i = 0; i < num_levels; ++i) {
    Price p(min_price_.price + i * tick_price_.price);
    buy_levels_[i].price = p;
    sell_levels_[i].price = p;
  }

  // Hash table: capacity must be a power of 2 >= 2 * max_orders.
  // Linear probing wraps with a bitmask, which requires power-of-2 size.
  size_t ht_cap = 2;
  while (ht_cap < 2 * max_orders)
    ht_cap <<= 1;
  order_id_to_index.assign(ht_cap, {0, 0, 0});
  ht_shift_ = static_cast<uint32_t>(64 - __builtin_ctzll(ht_cap));
}

// Fibonacci multiplicative hash: maps dense monotone OrderIds to [0, cap).
// GOLDEN = floor(2^64 / phi), distributes sequential IDs without clustering.
static constexpr uint64_t HT_GOLDEN = 11400714819323198485ULL;
static constexpr uint32_t HT_EMPTY = 0;
static constexpr uint32_t HT_OCCUPIED = 1;
static constexpr uint32_t HT_TOMBSTONE = 2;

int32_t FastBook::ht_find(OrderId id) const {
  const size_t mask = order_id_to_index.size() - 1;
  size_t i = (id * HT_GOLDEN) >> ht_shift_;
  while (true) {
    const HtSlot &s = order_id_to_index[i];
    if (s.pad == HT_EMPTY)
      return -1; // empty — key absent
    if (s.pad == HT_OCCUPIED && s.key == id)
      return s.val;
    i = (i + 1) & mask; // skip tombstones and mismatches
  }
}

void FastBook::ht_insert(OrderId id, int32_t pool_idx) {
  const size_t mask = order_id_to_index.size() - 1;
  size_t i = (id * HT_GOLDEN) >> ht_shift_;
  while (true) {
    HtSlot &s = order_id_to_index[i];
    if (s.pad != HT_OCCUPIED) { // empty or tombstone — claim this slot
      s.key = id;
      s.val = pool_idx;
      s.pad = HT_OCCUPIED;
      return;
    }
    i = (i + 1) & mask;
  }
}

void FastBook::ht_erase(OrderId id) {
  const size_t mask = order_id_to_index.size() - 1;
  size_t i = (id * HT_GOLDEN) >> ht_shift_;
  while (true) {
    HtSlot &s = order_id_to_index[i];
    if (s.pad == HT_EMPTY)
      return; // not found (should not happen)
    if (s.pad == HT_OCCUPIED && s.key == id) {
      s.pad = HT_TOMBSTONE; // keep probe chain intact
      return;
    }
    i = (i + 1) & mask;
  }
}

void FastBook::remove_order(FastOrderv2 &order, PriceLevel &level,
                            int32_t pool_idx, HierarchicalBitset &bitset,
                            Quantity quantity_to_remove) {
  // In some way you have to assert that order is actually in the level and
  // bitset is set for that price index

  assert(order.quantity >= quantity_to_remove &&
         "You can only remove at most the order's quantity");

  level.total_quantity -= quantity_to_remove;
  order.quantity -= quantity_to_remove;

  if (order.quantity.quantity != 0)
    return;

  if (order.prev != -1) {
    pool_[order.prev].next = order.next;
  } else {
    level.head = order.next; // Removing head
  }

  if (order.next != -1) {
    pool_[order.next].prev = order.prev;
  } else {
    level.tail = order.prev; // Removing tail
  }

  if (level.empty()) {
    bitset.reset_bit(price_to_index(order.price));
  }

  ht_erase(order.id);
  order = FastOrderv2(); // Reset order
  num_orders_--;
  free_list_.push_back(pool_idx); // Return to free list
}

void FastBook::cancel(OrderId order_id) {

  int32_t index = ht_find(order_id);

  if (index < 0)
    return;

  FastOrderv2 &order = pool_[index];

  size_t price_index = price_to_index(order.price);

  PriceLevel &level = (order.side == Side::BUY) ? buy_levels_[price_index]
                                                : sell_levels_[price_index];

  HierarchicalBitset &bitset =
      (order.side == Side::BUY) ? buy_bitset_ : sell_bitset_;

  remove_order(order, level, index, bitset, order.quantity);

  if (listener_) {
    listener_->on_order_canceled(order_id);
  }
}

void FastBook::modify(OrderId order_id, Quantity new_quantity) {
  if (new_quantity.quantity == 0) {
    cancel(order_id);
    return;
  }

  int32_t index = ht_find(order_id);
  if (index < 0)
    return;

  FastOrderv2 &order = pool_[index];
  size_t price_index = price_to_index(order.price);
  PriceLevel &level = (order.side == Side::BUY) ? buy_levels_[price_index]
                                                : sell_levels_[price_index];

  if (new_quantity > order.quantity)
    level.total_quantity += (new_quantity - order.quantity);
  else
    level.total_quantity -= (order.quantity - new_quantity);
  order.quantity = new_quantity;

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

    int32_t current_order_index = best_level.head;

    while (current_order_index != -1 && order.quantity.quantity > 0) {
      FastOrderv2 &current_order = pool_[current_order_index];
      Quantity executed_qty = std::min(order.quantity, current_order.quantity);
      order.quantity -= executed_qty;

      if (listener_) {
        listener_->on_order_filled(order.id, current_order.id, best_level.price,
                                   executed_qty);
      }

      remove_order(current_order, best_level, current_order_index, bitset,
                   executed_qty);
      current_order_index = best_level.head;
    }
  }
}

void FastBook::add_limit_order(Order &order) {
  if (order.price < min_price_ || order.price > max_price_ ||
      (order.price.price - min_price_.price) % tick_price_.price != 0) {
    if (listener_)
      listener_->on_order_canceled(order.id);
    return;
  }

  match_orders(order);

  if (order.quantity.quantity > 0) {
    assert(num_orders_ < max_orders && "Order pool exhausted");
    int32_t slot = free_list_.back();
    free_list_.pop_back();

    FastOrderv2 &order_slot = pool_[slot];

    size_t index = price_to_index(order.price);
    PriceLevel &level =
        (order.side == Side::BUY) ? buy_levels_[index] : sell_levels_[index];

    order_slot = {.id = order.id,
                  .price = order.price,
                  .quantity = order.quantity,
                  .prev = level.tail,
                  .next = -1,
                  .side = order.side,
                  .padding = {0, 0, 0}};

    if (level.tail != -1)
      pool_[level.tail].next = slot;

    level.tail = slot;

    if (level.head == -1)
      level.head = slot;
    level.total_quantity += order.quantity;
    num_orders_++;

    (order.side == Side::BUY ? buy_bitset_ : sell_bitset_).set_bit(index);

    ht_insert(order.id, slot);
    if (listener_)
      listener_->on_order_added(order);
  }
}

} // namespace lob
