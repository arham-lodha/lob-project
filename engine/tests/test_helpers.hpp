#pragma once
#include "event_listener.hpp"
#include "ref_book.hpp"
#include "types.h"
#include <gtest/gtest.h>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lob::test {

class TestListener : public EventListener {
public:
    std::vector<Event> events;
    std::unordered_map<OrderId, Quantity> active_orders;
    Quantity total_submitted{0};
    Quantity total_passive_filled{0};
    Quantity total_cancelled{0};

    void on_order_added(const Order &o) override {
        events.push_back(OrderAddedEvent{o});
        active_orders[o.id] = o.quantity;
        total_submitted += o.quantity;
    }

    void on_order_filled(OrderId a, OrderId p, Price px, Quantity q) override {
        events.push_back(FillEvent{a, p, px, q});
        total_passive_filled += q;
        auto it = active_orders.find(p);
        if (it != active_orders.end()) {
            it->second -= q;
            if (it->second.quantity == 0)
                active_orders.erase(it);
        }
    }

    void on_order_canceled(OrderId id, Quantity cancelled_qty, Quantity remaining_qty) override {
        events.push_back(OrderCanceledEvent{id, cancelled_qty, remaining_qty});
        total_cancelled += cancelled_qty;
        active_orders.erase(id);
    }

    void on_order_modified(OrderId id, Quantity q) override {
        events.push_back(OrderModifiedEvent{id, q});
        auto it = active_orders.find(id);
        if (it != active_orders.end()) {
            Quantity old_qty = it->second;
            if (q < old_qty)
                total_cancelled += (old_qty - q);
            else
                total_submitted += (q - old_qty);
            it->second = q;
        }
    }

    std::vector<FillEvent> fills() const {
        std::vector<FillEvent> result;
        for (const auto &e : events)
            if (const auto *f = std::get_if<FillEvent>(&e))
                result.push_back(*f);
        return result;
    }

    size_t fill_count() const {
        size_t n = 0;
        for (const auto &e : events)
            if (std::holds_alternative<FillEvent>(e)) ++n;
        return n;
    }

    Quantity total_resting() const {
        Quantity total{0};
        for (const auto &[id, qty] : active_orders)
            total += qty;
        return total;
    }

    // total_resting + total_passive_filled + total_cancelled == total_submitted
    bool conservation_holds() const {
        return (total_resting() + total_passive_filled + total_cancelled) ==
               total_submitted;
    }

    void clear() {
        events.clear();
        active_orders.clear();
        total_submitted      = Quantity{0};
        total_passive_filled = Quantity{0};
        total_cancelled      = Quantity{0};
    }
};

inline Order limit_buy(OrderId id, uint64_t price, uint32_t qty,
                       uint64_t seq = 0) {
    return Order(id, Price(price), SequenceNumber(seq), Quantity(qty), Side::BUY);
}

inline Order limit_sell(OrderId id, uint64_t price, uint32_t qty,
                        uint64_t seq = 0) {
    return Order(id, Price(price), SequenceNumber(seq), Quantity(qty), Side::SELL);
}

inline Order market_buy(OrderId id, uint32_t qty, uint64_t seq = 0) {
    return Order(id, SequenceNumber(seq), Quantity(qty), Side::BUY);
}

inline Order market_sell(OrderId id, uint32_t qty, uint64_t seq = 0) {
    return Order(id, SequenceNumber(seq), Quantity(qty), Side::SELL);
}

inline void check_no_cross(RefBook &book) {
    if (book.empty()) return;
    Price bid = book.best_bid();
    Price ask = book.best_ask();
    if (bid.price > 0 && ask.price > 0)
        EXPECT_LT(bid.price, ask.price)
            << "Crossed book: bid=" << bid.price << " ask=" << ask.price;
}

} // namespace lob::test
