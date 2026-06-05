#pragma once
#include "order_book.hpp"
#include <map>
#include <unordered_map>
#include <vector>
namespace lob {

struct OrderLocation {
  Price price;
  Side side;
};

class RefBook : public Orderbook {
private:
  // You can add any additional data members you need here
  std::map<Price, std::vector<Order>> buy_orders_;
  std::map<Price, std::vector<Order>> sell_orders_;
  std::unordered_map<OrderId, OrderLocation> order_id_to_location_;

public:
  RefBook() : Orderbook() {}
  RefBook(EventListener *listener) : Orderbook(listener) {}

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
