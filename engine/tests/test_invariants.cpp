#include "test_helpers.hpp"
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

using namespace lob;
using namespace lob::test;

class InvariantTest : public ::testing::Test {
protected:
    TestListener listener;
    RefBook book{&listener};
};

// --- Conservation ---

TEST_F(InvariantTest, ConservationAfterFullFill) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_sell(2, 100, 10, 2));
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(InvariantTest, ConservationAfterPartialFillBuyLarger) {
    book.add(limit_buy(1, 100, 15, 1));
    book.add(limit_sell(2, 100, 10, 2));
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(InvariantTest, ConservationAfterPartialFillSellLarger) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_sell(2, 100, 15, 2));
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(InvariantTest, ConservationAfterCancel) {
    book.add(limit_buy(1, 100, 10));
    book.add(limit_buy(2, 100, 5));
    book.cancel(1);
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(InvariantTest, ConservationAfterMultiLevelSweep) {
    book.add(limit_sell(1, 100, 5, 1));
    book.add(limit_sell(2, 101, 5, 2));
    book.add(limit_sell(3, 102, 5, 3));
    book.add(limit_buy(4, 102, 15, 4));
    EXPECT_TRUE(listener.conservation_holds());
}

TEST_F(InvariantTest, ConservationAfterModify) {
    book.add(limit_buy(1, 100, 10));
    book.modify(1, Quantity(6));
    EXPECT_TRUE(listener.conservation_holds());
    EXPECT_EQ(listener.total_resting().quantity, 6u);
}

TEST_F(InvariantTest, ConservationAfterMixedOperations) {
    book.add(limit_buy(1, 100, 10, 1));
    book.add(limit_buy(2, 99, 5, 2));
    book.add(limit_sell(3, 101, 8, 3));
    book.add(limit_sell(4, 100, 6, 4)); // crosses with buy@100
    book.cancel(2);
    book.modify(3, Quantity(4));
    EXPECT_TRUE(listener.conservation_holds());
    check_no_cross(book);
}

// --- No-cross ---

TEST_F(InvariantTest, NoCrossAfterManyAdds) {
    book.add(limit_buy(1, 100, 5));
    book.add(limit_buy(2,  99, 5));
    book.add(limit_sell(3, 101, 5));
    book.add(limit_sell(4, 102, 5));
    check_no_cross(book);
}

TEST_F(InvariantTest, NoCrossAfterFillThenRest) {
    book.add(limit_buy(1, 100, 5, 1));
    book.add(limit_sell(2, 100, 3, 2)); // partial fill, buy rests with 2
    check_no_cross(book);
}

// --- Property-based ---

RC_GTEST_PROP(PropertyTests, NoCrossAfterRandomLimitOrders, ()) {
    using Op = std::tuple<uint64_t, uint32_t, bool>;
    auto ops = *rc::gen::container<std::vector<Op>>(
        rc::gen::tuple(
            rc::gen::inRange<uint64_t>(1, 200),
            rc::gen::inRange<uint32_t>(1, 100),
            rc::gen::arbitrary<bool>()));

    TestListener listener;
    RefBook book(&listener);
    OrderId id = 1;

    for (auto &[price, qty, is_buy] : ops) {
        Order o = is_buy ? limit_buy(id, price, qty, id)
                         : limit_sell(id, price, qty, id);
        ++id;
        book.add(o);
        if (!book.empty()) {
            Price bid = book.best_bid();
            Price ask = book.best_ask();
            if (bid.price > 0 && ask.price > 0)
                RC_ASSERT(bid.price < ask.price);
        }
    }
}

RC_GTEST_PROP(PropertyTests, ConservationAfterRandomLimitOrders, ()) {
    using Op = std::tuple<uint64_t, uint32_t, bool>;
    auto ops = *rc::gen::container<std::vector<Op>>(
        rc::gen::tuple(
            rc::gen::inRange<uint64_t>(1, 200),
            rc::gen::inRange<uint32_t>(1, 100),
            rc::gen::arbitrary<bool>()));

    TestListener listener;
    RefBook book(&listener);
    OrderId id = 1;

    for (auto &[price, qty, is_buy] : ops) {
        Order o = is_buy ? limit_buy(id, price, qty, id)
                         : limit_sell(id, price, qty, id);
        ++id;
        book.add(o);
        RC_ASSERT(listener.conservation_holds());
    }
}
