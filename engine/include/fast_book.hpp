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
  // You can add any additional data members you need here
  Price min_price_;
  Price max_price_;
  Price tick_price_;
  size_t num_levels;
  size_t max_orders;
  size_t num_orders_ = 0;
  std::vector<PriceLevel> buy_levels_;
  std::vector<PriceLevel> sell_levels_;
  std::vector<HtSlot> order_id_to_index;
  uint32_t ht_shift_; // = 64 - log2(order_id_to_index.size())
  HierarchicalBitset sell_bitset_;
  HierarchicalBitset buy_bitset_;
  std::vector<FastOrderv2> pool_;
  std::vector<int32_t> free_list_;

  size_t price_to_index(Price price) const {
    return (price.price - min_price_.price) / tick_price_.price;
  }
  size_t capacity() const { return max_orders; }

  void remove_order(FastOrderv2 &order, PriceLevel &level, int32_t pool_idx,
                    HierarchicalBitset &bitset, Quantity quantity_to_remove);

  int32_t ht_find(OrderId id) const;
  void ht_insert(OrderId id, int32_t pool_idx);
  void ht_erase(OrderId id);

public:
  FastBook(EventListener *listener, Price min_price, Price max_price,
           Price tick_price, size_t max_orders);

  void cancel(OrderId order_id) override;
  void modify(OrderId order_id, Quantity new_quantity) override;
  Price best_bid() const override;
  Price best_ask() const override;
  Quantity total_quantity_at_price(Price price, Side side) const override;
  bool empty() const override;

protected:
  void execute_market_order(Order &order) override;
  void add_limit_order(Order &order) override;
  void match_orders(Order &incoming_order) override;
};

} // namespace lob
