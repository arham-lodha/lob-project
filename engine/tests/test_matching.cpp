#include "test_helpers.hpp"
#include <gtest/gtest.h>

using namespace lob;
using namespace lob::test;

class MatchingTest : public ::testing::Test {
protected:
    TestListener listener;
    RefBook book{&listener};
};

// --- Resting ---

TEST_F(MatchingTest, LimitBuyRestsWhenNoSell) {
    auto o = limit_buy(1, 100, 10);
    book.add(o);
    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<OrderAddedEvent>(listener.events[0]));
    EXPECT_EQ(book.best_bid().price, 100u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 10u);
    check_no_cross(book);
}

TEST_F(MatchingTest, LimitSellRestsWhenNoBuy) {
    auto o = limit_sell(1, 100, 10);
    book.add(o);
    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<OrderAddedEvent>(listener.events[0]));
    EXPECT_EQ(book.best_ask().price, 100u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::SELL).quantity, 10u);
    check_no_cross(book);
}

TEST_F(MatchingTest, NoCrossWhenPricesDontMatch) {
    auto buy  = limit_buy(1, 99, 10);
    auto sell = limit_sell(2, 101, 10);
    book.add(buy);
    book.add(sell);
    EXPECT_EQ(listener.fill_count(), 0u);
    EXPECT_EQ(book.best_bid().price, 99u);
    EXPECT_EQ(book.best_ask().price, 101u);
    check_no_cross(book);
}

TEST_F(MatchingTest, MultipleLevelsRestCorrectly) {
    book.add(limit_buy(1, 99, 5));
    book.add(limit_buy(2, 98, 5));
    book.add(limit_sell(3, 101, 5));
    book.add(limit_sell(4, 102, 5));
    EXPECT_EQ(listener.fill_count(), 0u);
    EXPECT_EQ(book.best_bid().price, 99u);
    EXPECT_EQ(book.best_ask().price, 101u);
    check_no_cross(book);
}

// --- Fills ---

TEST_F(MatchingTest, FullFillAtSamePrice) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_sell(2, 100, 10, 2));
    auto fills = listener.fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].aggressor, 2u);
    EXPECT_EQ(fills[0].passive,   1u);
    EXPECT_EQ(fills[0].price.price, 100u);
    EXPECT_EQ(fills[0].quantity.quantity, 10u);
    EXPECT_TRUE(book.empty());
    check_no_cross(book);
}

TEST_F(MatchingTest, PartialFillBuyLarger) {
    book.add(limit_buy(1, 100, 15, 1));
    book.add(limit_sell(2, 100, 10, 2));
    auto fills = listener.fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].quantity.quantity, 10u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 5u);
    EXPECT_FALSE(book.empty());
    check_no_cross(book);
}

TEST_F(MatchingTest, PartialFillSellLarger) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_sell(2, 100, 15, 2));
    auto fills = listener.fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].quantity.quantity, 10u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::SELL).quantity, 5u);
    EXPECT_FALSE(book.empty());
    check_no_cross(book);
}

TEST_F(MatchingTest, BuyMatchesCheapestAskFirst) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 101, 5, 2));
    book.add(limit_buy(3, 101, 5, 3));
    auto fills = listener.fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].passive, 1u);
    EXPECT_EQ(fills[0].price.price, 100u);
    EXPECT_EQ(book.best_ask().price, 101u);
    check_no_cross(book);
}

TEST_F(MatchingTest, SweepMultipleLevels) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 101, 5, 2));
    book.add(limit_sell(3, 102, 5, 3));
    book.add(limit_buy(4, 102, 15, 4));
    EXPECT_EQ(listener.fill_count(), 3u);
    EXPECT_TRUE(book.empty());
    check_no_cross(book);
}

// --- Price-time priority ---

TEST_F(MatchingTest, PriceTimePriorityFIFO) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 100, 5, 2));
    book.add(limit_buy(3, 100, 5, 3));
    auto fills = listener.fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].passive, 1u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::SELL).quantity, 5u);
    check_no_cross(book);
}

TEST_F(MatchingTest, BetterPriceTakesPriorityOverTime) {
    book.add(limit_sell(1, 101, 5, 1));
    book.add(limit_sell(2, 100, 5, 2));
    book.add(limit_buy(3, 101, 5, 3));
    auto fills = listener.fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].passive, 2u);       // cheaper sell (100) wins despite later time
    EXPECT_EQ(fills[0].price.price, 100u);
    check_no_cross(book);
}

// --- Market orders ---

TEST_F(MatchingTest, MarketBuyOnEmptyBook) {
    auto o = market_buy(1, 10);
    book.add(o);
    EXPECT_EQ(listener.fill_count(), 0u);
    EXPECT_TRUE(book.empty());
}

TEST_F(MatchingTest, MarketSellOnEmptyBook) {
    auto o = market_sell(1, 10);
    book.add(o);
    EXPECT_EQ(listener.fill_count(), 0u);
    EXPECT_TRUE(book.empty());
}

TEST_F(MatchingTest, MarketBuyFullFill) {
    book.add(limit_sell(1, 100, 10, 1));
    auto o = market_buy(2, 10, 2);
    book.add(o);
    ASSERT_EQ(listener.fill_count(), 1u);
    EXPECT_EQ(listener.fills()[0].quantity.quantity, 10u);
    EXPECT_TRUE(book.empty());
}

TEST_F(MatchingTest, MarketBuyPartialFill) {
    book.add(limit_sell(1, 100, 5, 1));
    auto o = market_buy(2, 10, 2);
    book.add(o);
    EXPECT_EQ(listener.fill_count(), 1u);
    EXPECT_TRUE(book.empty()); // sell consumed; market buy doesn't rest
}

TEST_F(MatchingTest, MarketBuySweepsMultipleLevels) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 101, 5, 2));
    book.add(limit_sell(3, 102, 5, 3));
    auto o = market_buy(4, 15, 4);
    book.add(o);
    EXPECT_EQ(listener.fill_count(), 3u);
    EXPECT_TRUE(book.empty());
}

TEST_F(MatchingTest, MarketSellFullFill) {
    book.add(limit_buy(1, 100, 10, 1));
    auto o = market_sell(2, 10, 2);
    book.add(o);
    ASSERT_EQ(listener.fill_count(), 1u);
    EXPECT_TRUE(book.empty());
}

TEST_F(MatchingTest, MarketSellSweepsMultipleLevels) {
    book.add(limit_buy(1, 102, 5, 1));
    book.add(limit_buy(2, 101, 5, 2));
    book.add(limit_buy(3, 100, 5, 3));
    auto o = market_sell(4, 15, 4);
    book.add(o);
    EXPECT_EQ(listener.fill_count(), 3u);
    EXPECT_TRUE(book.empty());
}

// --- Cancel ---

TEST_F(MatchingTest, CancelRemovesOrderFromBook) {
    book.add(limit_buy(1, 100, 10));
    book.cancel(1);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 0u);
    EXPECT_TRUE(book.empty());
    size_t cancel_count = 0;
    for (const auto &e : listener.events)
        if (std::holds_alternative<OrderCanceledEvent>(e)) ++cancel_count;
    EXPECT_EQ(cancel_count, 1u);
    check_no_cross(book);
}

TEST_F(MatchingTest, CancelOneOfManyAtSamePrice) {
    book.add(limit_sell(1, 100, 5));
    book.add(limit_sell(2, 100, 5));
    book.cancel(1);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::SELL).quantity, 5u);
    EXPECT_FALSE(book.empty());
    check_no_cross(book);
}

TEST_F(MatchingTest, CancelNonExistentIdIsNoop) {
    book.cancel(999);
    EXPECT_TRUE(book.empty());
    EXPECT_EQ(listener.events.size(), 0u);
}

// --- Modify ---

TEST_F(MatchingTest, ModifyQtyDown) {
    book.add(limit_buy(1, 100, 10));
    book.modify(1, Quantity(5));
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 5u);
    size_t mod_count = 0;
    for (const auto &e : listener.events)
        if (std::holds_alternative<OrderModifiedEvent>(e)) ++mod_count;
    EXPECT_EQ(mod_count, 1u);
    check_no_cross(book);
}

TEST_F(MatchingTest, ModifyNonExistentIdIsNoop) {
    book.modify(999, Quantity(5));
    EXPECT_TRUE(book.empty());
}
