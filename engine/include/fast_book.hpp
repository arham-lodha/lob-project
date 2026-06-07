#pragma once
#include "hierarchical_bitset.hpp"
#include "order_book.hpp"
#include "types.h"
#include <cstdint>
#include <vector>

namespace lob {

struct PriceLevel {
  Price price;
  int32_t head = -1;
  int32_t tail = -1;
  Quantity total_quantity = Quantity(0);

  bool empty() const { return head < 0; }
};

struct HtSlot {
  OrderId key;
  int32_t val; // pool index
  uint32_t
      pad; // Padding to get 16 bytes. We will use this as occupied vs not check
};

class FastBook : public Orderbook {
private:
  Price min_price_;
  Price max_price_;
  size_t num_levels;
  size_t num_orders_ = 0;
  std::vector<PriceLevel>
      levels_[2]; // [BUY=0, SELL=1], index via static_cast<uint8_t>(side)
  std::vector<HtSlot> order_id_to_index;
  uint32_t ht_shift_;             // = 64 - log2(order_id_to_index.size())
  HierarchicalBitset bitsets_[2]; // [BUY=0, SELL=1]
  std::vector<FastOrderv2> pool_;
  std::vector<int32_t> free_list_;

  size_t price_to_index(Price price) const {
    return price.price - min_price_.price;
  }

  void remove_order(FastOrderv2 &order, PriceLevel &level, int32_t pool_idx,
                    HierarchicalBitset &bitset, Quantity quantity_to_remove);

  int32_t ht_find(OrderId id) const;
  void ht_insert(OrderId id, int32_t pool_idx);
  void ht_erase(OrderId id);

  template <Side side, OrderType type> void add_order_impl(Order &order);

public:
  FastBook(EventListener *listener, Price min_price, Price max_price,
           size_t max_orders);

  void cancel(OrderId order_id) override;
  void modify(OrderId order_id, Quantity new_quantity) override;
  Price best_bid() const override;
  Price best_ask() const override;
  Quantity total_quantity_at_price(Price price, Side side) const override;
  bool empty() const override;

  void add(Order order) override;

protected:
  void execute_market_order(Order &order) override;
  void add_limit_order(Order &order) override;
  void match_orders(Order &incoming_order) override;

  template <Side side, OrderType type>
  void match_orders_impl(Order &incoming_order);
};

} // namespace lob
