#include "hierarchical_bitset.hpp"
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include <algorithm>
#include <vector>

using namespace lob;

// --- Structure ---

TEST(HierarchicalBitsetTest, OneTierForUpTo64Bits) {
    EXPECT_EQ(HierarchicalBitset(1).num_levels(),  1u);
    EXPECT_EQ(HierarchicalBitset(64).num_levels(), 1u);
}

TEST(HierarchicalBitsetTest, TwoTiersForUpTo4096Bits) {
    EXPECT_EQ(HierarchicalBitset(65).num_levels(),   2u);
    EXPECT_EQ(HierarchicalBitset(4096).num_levels(), 2u);
}

TEST(HierarchicalBitsetTest, ThreeTiersForUpTo262144Bits) {
    EXPECT_EQ(HierarchicalBitset(4097).num_levels(),   3u);
    EXPECT_EQ(HierarchicalBitset(262144).num_levels(), 3u);
}

TEST(HierarchicalBitsetTest, SizeReturnsNumBits) {
    EXPECT_EQ(HierarchicalBitset(1000).size(), 1000u);
}

// --- any() ---

TEST(HierarchicalBitsetTest, FreshBitsetIsEmpty) {
    EXPECT_FALSE(HierarchicalBitset(1000).any());
}

TEST(HierarchicalBitsetTest, AnyTrueAfterSet) {
    HierarchicalBitset bs(1000);
    bs.set_bit(42);
    EXPECT_TRUE(bs.any());
}

TEST(HierarchicalBitsetTest, AnyFalseAfterSetThenReset) {
    HierarchicalBitset bs(1000);
    bs.set_bit(42);
    bs.reset_bit(42);
    EXPECT_FALSE(bs.any());
}

// --- find_first_set_bit ---

TEST(HierarchicalBitsetTest, FindFirstSingleBit) {
    for (size_t i : {0u, 1u, 63u, 64u, 65u, 999u}) {
        HierarchicalBitset bs(1000);
        bs.set_bit(i);
        EXPECT_EQ(bs.find_first_set_bit(), i) << "bit=" << i;
    }
}

TEST(HierarchicalBitsetTest, FindFirstReturnsMinimum) {
    HierarchicalBitset bs(1000);
    bs.set_bit(500);
    bs.set_bit(100);
    bs.set_bit(750);
    EXPECT_EQ(bs.find_first_set_bit(), 100u);
}

TEST(HierarchicalBitsetTest, FindFirstUpdatesAfterResetOfMinimum) {
    HierarchicalBitset bs(1000);
    bs.set_bit(100);
    bs.set_bit(200);
    bs.reset_bit(100);
    EXPECT_EQ(bs.find_first_set_bit(), 200u);
}

TEST(HierarchicalBitsetTest, FindFirstAcrossWordBoundary) {
    HierarchicalBitset bs(1000);
    bs.set_bit(63);
    bs.set_bit(64);
    EXPECT_EQ(bs.find_first_set_bit(), 63u);
    bs.reset_bit(63);
    EXPECT_EQ(bs.find_first_set_bit(), 64u);
}

// --- find_last_set_bit ---

TEST(HierarchicalBitsetTest, FindLastSingleBit) {
    for (size_t i : {0u, 1u, 63u, 64u, 65u, 999u}) {
        HierarchicalBitset bs(1000);
        bs.set_bit(i);
        EXPECT_EQ(bs.find_last_set_bit(), i) << "bit=" << i;
    }
}

TEST(HierarchicalBitsetTest, FindLastReturnsMaximum) {
    HierarchicalBitset bs(1000);
    bs.set_bit(100);
    bs.set_bit(500);
    bs.set_bit(750);
    EXPECT_EQ(bs.find_last_set_bit(), 750u);
}

TEST(HierarchicalBitsetTest, FindLastUpdatesAfterResetOfMaximum) {
    HierarchicalBitset bs(1000);
    bs.set_bit(100);
    bs.set_bit(200);
    bs.reset_bit(200);
    EXPECT_EQ(bs.find_last_set_bit(), 100u);
}

TEST(HierarchicalBitsetTest, FindLastAcrossWordBoundary) {
    HierarchicalBitset bs(1000);
    bs.set_bit(63);
    bs.set_bit(64);
    EXPECT_EQ(bs.find_last_set_bit(), 64u);
    bs.reset_bit(64);
    EXPECT_EQ(bs.find_last_set_bit(), 63u);
}

// --- Idempotency ---

TEST(HierarchicalBitsetTest, DoubleSetIsIdempotent) {
    HierarchicalBitset bs(1000);
    bs.set_bit(42);
    bs.set_bit(42);
    bs.reset_bit(42);
    EXPECT_FALSE(bs.any());
}

// --- Multi-tier boundary ---

TEST(HierarchicalBitsetTest, BitsAtTierBoundary) {
    // 4095 and 4096 straddle the 2-tier / 3-tier boundary
    HierarchicalBitset bs(262144);
    bs.set_bit(4095);
    bs.set_bit(4096);
    EXPECT_EQ(bs.find_first_set_bit(), 4095u);
    EXPECT_EQ(bs.find_last_set_bit(),  4096u);
    bs.reset_bit(4095);
    EXPECT_EQ(bs.find_first_set_bit(), 4096u);
}

// --- Property-based ---

RC_GTEST_PROP(HierarchicalBitsetPropertyTest, FindFirstIsMinimum, ()) {
    const size_t N = 1000;
    auto indices = *rc::gen::nonEmpty(
        rc::gen::container<std::vector<size_t>>(
            rc::gen::inRange<size_t>(0, N)));

    HierarchicalBitset bs(N);
    for (size_t i : indices)
        bs.set_bit(i);

    size_t expected = *std::min_element(indices.begin(), indices.end());
    RC_ASSERT(bs.find_first_set_bit() == expected);
}

RC_GTEST_PROP(HierarchicalBitsetPropertyTest, FindLastIsMaximum, ()) {
    const size_t N = 1000;
    auto indices = *rc::gen::nonEmpty(
        rc::gen::container<std::vector<size_t>>(
            rc::gen::inRange<size_t>(0, N)));

    HierarchicalBitset bs(N);
    for (size_t i : indices)
        bs.set_bit(i);

    size_t expected = *std::max_element(indices.begin(), indices.end());
    RC_ASSERT(bs.find_last_set_bit() == expected);
}

RC_GTEST_PROP(HierarchicalBitsetPropertyTest, SetThenResetAllLeavesEmpty, ()) {
    const size_t N = 1000;
    auto indices = *rc::gen::nonEmpty(
        rc::gen::container<std::vector<size_t>>(
            rc::gen::inRange<size_t>(0, N)));

    HierarchicalBitset bs(N);
    for (size_t i : indices)
        bs.set_bit(i);
    for (size_t i : indices)
        bs.reset_bit(i);

    RC_ASSERT(!bs.any());
}

RC_GTEST_PROP(HierarchicalBitsetPropertyTest, FindFirstMatchesBruteForce, ()) {
    const size_t N = 300; // small enough for brute-force scan
    auto indices = *rc::gen::nonEmpty(
        rc::gen::container<std::vector<size_t>>(
            rc::gen::inRange<size_t>(0, N)));

    HierarchicalBitset bs(N);
    std::vector<bool> ref(N, false);
    for (size_t i : indices) {
        bs.set_bit(i);
        ref[i] = true;
    }

    size_t brute = 0;
    while (brute < N && !ref[brute]) ++brute;

    RC_ASSERT(bs.find_first_set_bit() == brute);
}
