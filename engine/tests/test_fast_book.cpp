#include "fast_book.hpp"
#include "ref_book.hpp"
#include "test_helpers.hpp"
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

using namespace lob;
using namespace lob::test;

static const Price  MIN_PRICE{1};
static const Price  MAX_PRICE{1000};
static const Price  TICK{1};
static const size_t MAX_ORDERS = 10000;

static void check_no_cross(Orderbook &book) {
    if (book.empty()) return;
    Price bid = book.best_bid();
    Price ask = book.best_ask();
    if (bid.price > 0 && ask.price > 0)
        EXPECT_LT(bid.price, ask.price)
            << "Crossed book: bid=" << bid.price << " ask=" << ask.price;
}

static bool fills_equal(const std::vector<FillEvent> &a,
                        const std::vector<FillEvent> &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].aggressor        != b[i].aggressor        ||
            a[i].passive          != b[i].passive           ||
            a[i].price.price      != b[i].price.price       ||
            a[i].quantity.quantity != b[i].quantity.quantity)
            return false;
    }
    return true;
}

// ============================================================
// Standalone FastBook tests
// ============================================================

class FastBookTest : public ::testing::Test {
protected:
    TestListener listener;
    FastBook book{&listener, MIN_PRICE, MAX_PRICE, TICK, MAX_ORDERS};
};

// --- Resting ---

TEST_F(FastBookTest, LimitBuyRestsWhenNoSell) {
    book.add(limit_buy(1, 100, 10));
    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<OrderAddedEvent>(listener.events[0]));
    EXPECT_EQ(book.best_bid().price, 100u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 10u);
    check_no_cross(book);
}

TEST_F(FastBookTest, LimitSellRestsWhenNoBuy) {
    book.add(limit_sell(1, 100, 10));
    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<OrderAddedEvent>(listener.events[0]));
    EXPECT_EQ(book.best_ask().price, 100u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::SELL).quantity, 10u);
    check_no_cross(book);
}

TEST_F(FastBookTest, MultipleLevelsRestCorrectly) {
    book.add(limit_buy(1, 100, 5));
    book.add(limit_buy(2,  99, 5));
    book.add(limit_sell(3, 101, 5));
    book.add(limit_sell(4, 102, 5));
    EXPECT_EQ(book.best_bid().price, 100u);
    EXPECT_EQ(book.best_ask().price, 101u);
    check_no_cross(book);
}

// --- Matching ---

TEST_F(FastBookTest, NoCrossWhenPricesDontMatch) {
    book.add(limit_buy(1, 99, 10));
    book.add(limit_sell(2, 101, 10));
    EXPECT_EQ(listener.fill_count(), 0u);
    check_no_cross(book);
}

TEST_F(FastBookTest, ExactMatch) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_sell(2, 100, 10, 2));
    EXPECT_EQ(listener.fill_count(), 1u);
    EXPECT_TRUE(book.empty());
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, PartialFillBuyLarger) {
    book.add(limit_buy(1, 100, 15, 1));
    book.add(limit_sell(2, 100, 10, 2));
    EXPECT_EQ(listener.fill_count(), 1u);
    EXPECT_EQ(book.best_bid().price, 100u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 5u);
    check_no_cross(book);
}

TEST_F(FastBookTest, PartialFillSellLarger) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_sell(2, 100, 15, 2));
    EXPECT_EQ(listener.fill_count(), 1u);
    EXPECT_EQ(book.best_ask().price, 100u);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::SELL).quantity, 5u);
    check_no_cross(book);
}

TEST_F(FastBookTest, MultiLevelSweep) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 101, 5, 2));
    book.add(limit_sell(3, 102, 5, 3));
    book.add(limit_buy(4, 102, 15, 4));
    EXPECT_EQ(listener.fill_count(), 3u);
    EXPECT_TRUE(book.empty());
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, LimitBuyDoesNotMatchBeyondItsPrice) {
    book.add(limit_sell(1, 101, 10, 1));
    book.add(limit_buy(2, 100, 10, 2));   // limit is below the ask — no match
    EXPECT_EQ(listener.fill_count(), 0u);
    EXPECT_EQ(book.best_bid().price, 100u);
    EXPECT_EQ(book.best_ask().price, 101u);
}

TEST_F(FastBookTest, MarketBuySweepsAllSells) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 101, 5, 2));
    book.add(market_buy(3, 10, 3));
    EXPECT_EQ(listener.fill_count(), 2u);
    EXPECT_TRUE(book.empty());
}

TEST_F(FastBookTest, MarketSellSweepsAllBuys) {
    book.add(limit_buy(1, 100, 5, 1));
    book.add(limit_buy(2,  99, 5, 2));
    book.add(market_sell(3, 10, 3));
    EXPECT_EQ(listener.fill_count(), 2u);
    EXPECT_TRUE(book.empty());
}

// --- Price-time priority ---

TEST_F(FastBookTest, PriceTimePriorityWithinLevel) {
    book.add(limit_sell(1, 100, 5, 1));   // rests first
    book.add(limit_sell(2, 100, 5, 2));   // rests second
    book.add(limit_buy(3, 100, 5, 3));    // aggressor — should fill order 1
    auto f = listener.fills();
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f[0].passive, 1u);
}

TEST_F(FastBookTest, PricePriorityAcrossLevels) {
    book.add(limit_sell(1, 101, 5, 1));   // worse price
    book.add(limit_sell(2, 100, 5, 2));   // better price — rests second but fills first
    book.add(limit_buy(3, 101, 5, 3));
    auto f = listener.fills();
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f[0].passive, 2u);          // order 2 at price 100 fills, not order 1 at 101
}

// --- Cancel / Modify ---

TEST_F(FastBookTest, CancelRestingOrder) {
    book.add(limit_buy(1, 100, 10));
    book.cancel(1);
    EXPECT_TRUE(book.empty());
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, CancelNonExistentOrderIsNoop) {
    book.add(limit_buy(1, 100, 10));
    book.cancel(99);
    EXPECT_EQ(book.best_bid().price, 100u);
}

TEST_F(FastBookTest, CancelOneOfManyAtLevel) {
    book.add(limit_buy(1, 100, 5));
    book.add(limit_buy(2, 100, 5));
    book.cancel(1);
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 5u);
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, ModifyReduceQuantity) {
    book.add(limit_buy(1, 100, 10));
    book.modify(1, Quantity(4));
    EXPECT_EQ(book.total_quantity_at_price(Price(100), Side::BUY).quantity, 4u);
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, ModifyToZeroCancelsOrder) {
    book.add(limit_buy(1, 100, 10));
    book.modify(1, Quantity(0));
    EXPECT_TRUE(book.empty());
}

// --- Bounds & tick validation ---

TEST_F(FastBookTest, OutOfRangePriceAboveMax) {
    book.add(limit_buy(1, 1001, 10));
    EXPECT_TRUE(book.empty());
}

TEST_F(FastBookTest, OutOfRangePriceBelowMin) {
    book.add(limit_sell(1, 0, 10));
    EXPECT_TRUE(book.empty());
}

TEST_F(FastBookTest, MisalignedTickRejected) {
    TestListener l2;
    FastBook book2{&l2, Price(2), Price(100), Price(2), 100};
    book2.add(limit_buy(1, 3, 10));   // price 3 not on tick 2
    EXPECT_TRUE(book2.empty());
}

TEST_F(FastBookTest, AlignedTickAccepted) {
    TestListener l2;
    FastBook book2{&l2, Price(2), Price(100), Price(2), 100};
    book2.add(limit_buy(1, 4, 10));   // price 4 on tick 2
    EXPECT_EQ(book2.best_bid().price, 4u);
}

TEST_F(FastBookTest, TotalQuantityOutOfRangeReturnsZero) {
    EXPECT_EQ(book.total_quantity_at_price(Price(0),    Side::BUY).quantity, 0u);
    EXPECT_EQ(book.total_quantity_at_price(Price(1001), Side::BUY).quantity, 0u);
}

// --- Invariants ---

TEST_F(FastBookTest, ConservationAfterFullFill) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_sell(2, 100, 10, 2));
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, ConservationAfterPartialFill) {
    book.add(limit_buy(1, 100, 15, 1));
    book.add(limit_sell(2, 100, 10, 2));
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, ConservationAfterCancel) {
    book.add(limit_buy(1, 100, 10));
    book.add(limit_buy(2, 100, 5));
    book.cancel(1);
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(FastBookTest, ConservationAfterMultiLevelSweep) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 101, 5, 2));
    book.add(limit_sell(3, 102, 5, 3));
    book.add(limit_buy(4, 102, 15, 4));
    EXPECT_TRUE(listener.conservation_holds());
}

// ============================================================
// Differential tests — FastBook must produce identical output to RefBook
// ============================================================

class DiffTest : public ::testing::Test {
protected:
    TestListener ref_l, fast_l;
    RefBook      ref{&ref_l};
    FastBook     fast{&fast_l, MIN_PRICE, MAX_PRICE, TICK, MAX_ORDERS};

    void step(Order o) {
        ref_l.clear();
        fast_l.clear();
        ref.add(o);
        fast.add(o);
        EXPECT_TRUE(fills_equal(ref_l.fills(), fast_l.fills()))
            << "Fill mismatch on order id=" << o.id;
        EXPECT_EQ(ref.best_bid().price,  fast.best_bid().price);
        EXPECT_EQ(ref.best_ask().price,  fast.best_ask().price);
        EXPECT_EQ(ref.empty(),           fast.empty());
    }

    void step_cancel(OrderId id) {
        ref.cancel(id);
        fast.cancel(id);
        EXPECT_EQ(ref.best_bid().price, fast.best_bid().price);
        EXPECT_EQ(ref.best_ask().price, fast.best_ask().price);
        EXPECT_EQ(ref.empty(),          fast.empty());
    }
};

TEST_F(DiffTest, SingleLimitBuy)           { step(limit_buy(1, 100, 10)); }
TEST_F(DiffTest, SingleLimitSell)          { step(limit_sell(1, 100, 10)); }
TEST_F(DiffTest, ExactMatch) {
    step(limit_buy(1, 100, 10, 1));
    step(limit_sell(2, 100, 10, 2));
}
TEST_F(DiffTest, PartialFill) {
    step(limit_buy(1, 100, 15, 1));
    step(limit_sell(2, 100, 10, 2));
}
TEST_F(DiffTest, MultiLevelSweep) {
    step(limit_sell(1, 100, 5, 1));
    step(limit_sell(2, 101, 5, 2));
    step(limit_sell(3, 102, 5, 3));
    step(limit_buy(4, 102, 15, 4));
}
TEST_F(DiffTest, CancelThenMatch) {
    step(limit_sell(1, 100, 10, 1));
    step(limit_sell(2, 100, 10, 2));
    step_cancel(1);
    step(limit_buy(3, 100, 10, 3));
}
TEST_F(DiffTest, ModifyThenMatch) {
    step(limit_buy(1, 100, 20, 1));
    ref.modify(1, Quantity(5));
    fast.modify(1, Quantity(5));
    step(limit_sell(2, 100, 5, 2));
}

// --- Property-based differential ---

RC_GTEST_PROP(DiffPropertyTest, SameFillsAndStateAsRefBook, ()) {
    using Op = std::tuple<uint64_t, uint32_t, bool>;
    auto ops = *rc::gen::nonEmpty(
        rc::gen::container<std::vector<Op>>(
            rc::gen::tuple(
                rc::gen::inRange<uint64_t>(MIN_PRICE.price, 200u),
                rc::gen::inRange<uint32_t>(1u, 100u),
                rc::gen::arbitrary<bool>())));

    TestListener ref_l, fast_l;
    RefBook  ref(&ref_l);
    FastBook fast(&fast_l, MIN_PRICE, MAX_PRICE, TICK, MAX_ORDERS);

    OrderId id = 1;
    for (auto &[price, qty, is_buy] : ops) {
        ref_l.clear();
        fast_l.clear();

        Order o = is_buy ? limit_buy(id, price, qty, id)
                         : limit_sell(id, price, qty, id);
        ++id;

        ref.add(o);
        fast.add(o);

        auto rf = ref_l.fills();
        auto ff = fast_l.fills();
        RC_ASSERT(rf.size() == ff.size());
        for (size_t i = 0; i < rf.size(); ++i) {
            RC_ASSERT(rf[i].aggressor         == ff[i].aggressor);
            RC_ASSERT(rf[i].passive           == ff[i].passive);
            RC_ASSERT(rf[i].price.price       == ff[i].price.price);
            RC_ASSERT(rf[i].quantity.quantity == ff[i].quantity.quantity);
        }

        RC_ASSERT(ref.best_bid().price == fast.best_bid().price);
        RC_ASSERT(ref.best_ask().price == fast.best_ask().price);
        RC_ASSERT(ref.empty()          == fast.empty());
    }
}
