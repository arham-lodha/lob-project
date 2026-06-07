#pragma once
#include "hierarchical_bitset.hpp"
#include "order_book.hpp"
#include <unordered_map>
#include <vector>

namespace lob {

struct PriceLevel {
  Price price;
  FastOrder *head;
  FastOrder *tail;
  Quantity total_quantity;

  bool empty() const { return head == nullptr; }
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
  std::unordered_map<OrderId, FastOrder *> order_id_to_order_;
  HierarchicalBitset sell_bitset_;
  HierarchicalBitset buy_bitset_;
  std::vector<FastOrder> pool_;
  std::vector<FastOrder *> free_list_;

  size_t price_to_index(Price price) const {
    return (price.price - min_price_.price) / tick_price_.price;
  }
  size_t capacity() const { return max_orders; }

  void remove_order(FastOrder *order, PriceLevel &level,
                    HierarchicalBitset &bitset, Quantity quantity_to_remove);

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
