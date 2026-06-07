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
                   size_t max_orders)
    : Orderbook(listener), min_price_(min_price), max_price_(max_price),
      num_levels((max_price.price - min_price.price) + 1), ht_shift_(0),
      bitsets_{HierarchicalBitset(num_levels), HierarchicalBitset(num_levels)},
      pool_(max_orders), free_list_() {

  free_list_.reserve(max_orders);
  for (size_t i = 0; i < pool_.size(); ++i) {
    free_list_.push_back(i);
  }

  levels_[0].resize(num_levels);
  levels_[1].resize(num_levels);

  for (size_t i = 0; i < num_levels; ++i) {
    Price p(min_price_.price + i);
    levels_[0][i].price = p;
    levels_[1][i].price = p;
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

int32_t FastBook::ht_find(OrderId id) const {
  const size_t mask = order_id_to_index.size() - 1;
  size_t i = (id * HT_GOLDEN) >> ht_shift_;
  while (true) {
    const HtSlot &s = order_id_to_index[i];
    if (s.pad == HT_EMPTY)
      return -1;
    if (s.key == id)
      return s.val;
    i = (i + 1) & mask;
  }
}

void FastBook::ht_insert(OrderId id, int32_t pool_idx) {
  const size_t mask = order_id_to_index.size() - 1;
  size_t i = (id * HT_GOLDEN) >> ht_shift_;
  while (true) {
    HtSlot &s = order_id_to_index[i];
    if (s.pad == HT_EMPTY) {
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
      return;
    if (s.key == id)
      break;
    i = (i + 1) & mask;
  }

  // Backward-shift deletion: pull the closest displaceable element into the
  // hole, repeat until the cluster end (empty slot) is reached.
  // Element at j (natural pos h) can fill hole at i when it probed through i:
  // ((j - h) & mask) >= ((j - i) & mask).
  while (true) {
    size_t j = i;
    while (true) {
      j = (j + 1) & mask;
      if (order_id_to_index[j].pad == HT_EMPTY) {
        order_id_to_index[i].pad = HT_EMPTY;
        return;
      }
      size_t h = (order_id_to_index[j].key * HT_GOLDEN) >> ht_shift_;
      if (((j - h) & mask) >= ((j - i) & mask))
        break;
    }
    order_id_to_index[i] = order_id_to_index[j];
    i = j;
  }
}

void FastBook::remove_order(FastOrderv2 &order, PriceLevel &level,
                            int32_t pool_idx, HierarchicalBitset &bitset,
                            Quantity quantity_to_remove) {
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
  num_orders_--;
  free_list_.push_back(pool_idx); // Return to free list
}

void FastBook::cancel(OrderId order_id) {
  int32_t index = ht_find(order_id);

  if (index < 0)
    return;

  FastOrderv2 &order = pool_[index];

  size_t price_index = price_to_index(order.price);
  uint8_t side_index = static_cast<uint8_t>(order.side);
  PriceLevel &level = levels_[side_index][price_index];
  HierarchicalBitset &bitset = bitsets_[side_index];

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
  PriceLevel &level = levels_[static_cast<uint8_t>(order.side)][price_index];

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
  if (!bitsets_[0].any())
    return Price{0};
  return levels_[0][bitsets_[0].find_last_set_bit()].price;
}

Price FastBook::best_ask() const {
  if (!bitsets_[1].any())
    return Price{0};
  return levels_[1][bitsets_[1].find_first_set_bit()].price;
}

Quantity FastBook::total_quantity_at_price(Price price, Side side) const {
  if (price.price < min_price_.price || price > max_price_)
    return Quantity{0};
  size_t price_index = price_to_index(price);
  return levels_[static_cast<uint8_t>(side)][price_index].total_quantity;
}

bool FastBook::empty() const {
  return !bitsets_[0].any() && !bitsets_[1].any();
}

void FastBook::execute_market_order(Order &order) { match_orders(order); }
void FastBook::add_limit_order(Order &order) { add(order); }

void FastBook::match_orders(Order &order) {
  if (order.type == OrderType::LIMIT) {
    if (order.side == Side::BUY) {
      match_orders_impl<Side::BUY, OrderType::LIMIT>(order);
    } else {
      match_orders_impl<Side::SELL, OrderType::LIMIT>(order);
    }
  } else {
    if (order.side == Side::BUY) {
      match_orders_impl<Side::BUY, OrderType::MARKET>(order);
    } else {
      match_orders_impl<Side::SELL, OrderType::MARKET>(order);
    }
  }
}

template <Side side, OrderType type>
void FastBook::match_orders_impl(Order &order) {
  constexpr uint8_t opp = 1 - static_cast<uint8_t>(side);
  HierarchicalBitset &bitset = bitsets_[opp];

  if (!bitset.any())
    return;

  std::vector<PriceLevel> &price_level_array = levels_[opp];

  while (order.quantity.quantity > 0 && bitset.any()) {

    size_t best_index;

    if constexpr (side == Side::BUY) {
      best_index = bitset.find_first_set_bit();
    } else {
      best_index = bitset.find_last_set_bit();
    }

    PriceLevel &best_level = price_level_array[best_index];

    if constexpr (type == OrderType::LIMIT) {
      if constexpr (side == Side::BUY) {
        if (best_level.price > order.price)
          return;
      } else {
        if (best_level.price < order.price)
          return;
      }
    }

    int32_t current_order_index = best_level.head;

    while (current_order_index != -1 && order.quantity.quantity > 0) {
      FastOrderv2 &current_order = pool_[current_order_index];
      int32_t next_order_index = current_order.next;
      if (next_order_index != -1)
        __builtin_prefetch(&pool_[next_order_index], 0, 1);

      Quantity executed_qty = std::min(order.quantity, current_order.quantity);
      order.quantity -= executed_qty;

      if (listener_) {
        listener_->on_order_filled(order.id, current_order.id, best_level.price,
                                   executed_qty);
      }

      remove_order(current_order, best_level, current_order_index, bitset,
                   executed_qty);
      current_order_index = next_order_index;
    }
  }
}


template <Side side, OrderType type>
void FastBook::add_order_impl(Order &order) {

  if constexpr (type == OrderType::LIMIT) {
    if (order.price < min_price_ || order.price > max_price_) {
      if (listener_)
        listener_->on_order_canceled(order.id);

      return;
    }
  }

  match_orders_impl<side, type>(order);

  if (order.quantity.quantity == 0)
    return;

  if constexpr (type == OrderType::MARKET) {
    return;
  }

  if constexpr (type == OrderType::LIMIT) {
    assert(num_orders_ < pool_.size() && "Order pool exhausted");

    constexpr uint8_t side_index = (side == Side::BUY) ? 0 : 1;
    HierarchicalBitset &bitset = bitsets_[side_index];
    std::vector<PriceLevel> &price_level_array = levels_[side_index];

    size_t index = price_to_index(order.price);
    PriceLevel &level = price_level_array[index];

    int32_t slot = free_list_.back();
    free_list_.pop_back();

    FastOrderv2 &order_slot = pool_[slot];

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

    bitset.set_bit(index);

    ht_insert(order.id, slot);
    if (listener_)
      listener_->on_order_added(order);
  }
}

void FastBook::add(Order order) {
  if (order.type == OrderType::LIMIT) {
    if (order.side == Side::BUY)
      add_order_impl<Side::BUY, OrderType::LIMIT>(order);
    else
      add_order_impl<Side::SELL, OrderType::LIMIT>(order);
  } else {
    if (order.side == Side::BUY)
      add_order_impl<Side::BUY, OrderType::MARKET>(order);
    else
      add_order_impl<Side::SELL, OrderType::MARKET>(order);
  }
}

} // namespace lob
