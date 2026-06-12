#include "../include/codec.h"
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include <cstring>

using namespace lob::codec;
using namespace lob::proto;

// ── Round-trip tests ──────────────────────────────────────────────────────────

TEST(Codec, EnterOrderRoundTrip) {
    EnterOrder orig{};
    orig.side     = 1;
    orig.quantity = 500;
    orig.order_id = 42;
    orig.price    = 100'0000;
    orig.seq_num  = 7;

    std::byte buf[sizeof(EnterOrder)];
    std::memcpy(buf, &orig, sizeof(EnterOrder));

    EnterOrder decoded{};
    ASSERT_TRUE(decode(buf, sizeof(buf), decoded));

    EXPECT_EQ(decoded.side,     orig.side);
    EXPECT_EQ(decoded.quantity, orig.quantity);
    EXPECT_EQ(decoded.order_id, orig.order_id);
    EXPECT_EQ(decoded.price,    orig.price);
    EXPECT_EQ(decoded.seq_num,  orig.seq_num);
}

TEST(Codec, CancelOrderRoundTrip) {
    CancelOrder orig{};
    orig.quantity      = 200;
    orig.order_id      = 99;
    orig.seq_num       = 3;
    orig.remaining_qty = 800;

    std::byte buf[sizeof(CancelOrder)];
    std::memcpy(buf, &orig, sizeof(CancelOrder));

    CancelOrder decoded{};
    ASSERT_TRUE(decode(buf, sizeof(buf), decoded));

    EXPECT_EQ(decoded.quantity,      orig.quantity);
    EXPECT_EQ(decoded.order_id,      orig.order_id);
    EXPECT_EQ(decoded.seq_num,       orig.seq_num);
    EXPECT_EQ(decoded.remaining_qty, orig.remaining_qty);
}

TEST(Codec, ReplaceOrderRoundTrip) {
    ReplaceOrder orig{};
    orig.new_qty   = 300;
    orig.order_id  = 55;
    orig.new_price = 200'0000;
    orig.seq_num   = 12;

    std::byte buf[sizeof(ReplaceOrder)];
    std::memcpy(buf, &orig, sizeof(ReplaceOrder));

    ReplaceOrder decoded{};
    ASSERT_TRUE(decode(buf, sizeof(buf), decoded));

    EXPECT_EQ(decoded.new_qty,   orig.new_qty);
    EXPECT_EQ(decoded.order_id,  orig.order_id);
    EXPECT_EQ(decoded.new_price, orig.new_price);
    EXPECT_EQ(decoded.seq_num,   orig.seq_num);
}

TEST(Codec, OrderAddedRoundTrip) {
    OrderAdded orig{};
    orig.side     = 0;
    orig.qty      = 100;
    orig.out_seq  = 1;
    orig.order_id = 10;
    orig.price    = 150'0000;

    std::byte buf[sizeof(OrderAdded)];
    ASSERT_EQ(encode(orig, buf), sizeof(OrderAdded));

    OrderAdded decoded{};
    std::memcpy(&decoded, buf, sizeof(OrderAdded));

    EXPECT_EQ(decoded.side,     orig.side);
    EXPECT_EQ(decoded.qty,      orig.qty);
    EXPECT_EQ(decoded.out_seq,  orig.out_seq);
    EXPECT_EQ(decoded.order_id, orig.order_id);
    EXPECT_EQ(decoded.price,    orig.price);
}

TEST(Codec, OrderExecutedRoundTrip) {
    OrderExecuted orig{};
    orig.out_seq       = 5;
    orig.order_id      = 20;
    orig.aggressor_id  = 21;
    orig.price         = 175'0000;
    orig.exec_qty      = 50;
    orig.remaining_qty = 50;

    std::byte buf[sizeof(OrderExecuted)];
    ASSERT_EQ(encode(orig, buf), sizeof(OrderExecuted));

    OrderExecuted decoded{};
    std::memcpy(&decoded, buf, sizeof(OrderExecuted));

    EXPECT_EQ(decoded.out_seq,       orig.out_seq);
    EXPECT_EQ(decoded.order_id,      orig.order_id);
    EXPECT_EQ(decoded.aggressor_id,  orig.aggressor_id);
    EXPECT_EQ(decoded.price,         orig.price);
    EXPECT_EQ(decoded.exec_qty,      orig.exec_qty);
    EXPECT_EQ(decoded.remaining_qty, orig.remaining_qty);
}

TEST(Codec, OrderCancelledRoundTrip) {
    OrderCancelled orig{};
    orig.cancelled_qty = 75;
    orig.out_seq       = 8;
    orig.order_id      = 33;

    std::byte buf[sizeof(OrderCancelled)];
    ASSERT_EQ(encode(orig, buf), sizeof(OrderCancelled));

    OrderCancelled decoded{};
    std::memcpy(&decoded, buf, sizeof(OrderCancelled));

    EXPECT_EQ(decoded.cancelled_qty, orig.cancelled_qty);
    EXPECT_EQ(decoded.out_seq,       orig.out_seq);
    EXPECT_EQ(decoded.order_id,      orig.order_id);
}

TEST(Codec, TradeRoundTrip) {
    Trade orig{};
    orig.qty          = 100;
    orig.out_seq      = 9;
    orig.trade_id     = 1001;
    orig.passive_id   = 40;
    orig.aggressor_id = 41;
    orig.price        = 120'0000;

    std::byte buf[sizeof(Trade)];
    ASSERT_EQ(encode(orig, buf), sizeof(Trade));

    Trade decoded{};
    std::memcpy(&decoded, buf, sizeof(Trade));

    EXPECT_EQ(decoded.qty,          orig.qty);
    EXPECT_EQ(decoded.out_seq,      orig.out_seq);
    EXPECT_EQ(decoded.trade_id,     orig.trade_id);
    EXPECT_EQ(decoded.passive_id,   orig.passive_id);
    EXPECT_EQ(decoded.aggressor_id, orig.aggressor_id);
    EXPECT_EQ(decoded.price,        orig.price);
}

// ── msg_size consistency ──────────────────────────────────────────────────────

TEST(Codec, MsgSizeMatchesSizeof) {
    EXPECT_EQ(msg_size(MsgType::EnterOrder),   sizeof(EnterOrder));
    EXPECT_EQ(msg_size(MsgType::CancelOrder),  sizeof(CancelOrder));
    EXPECT_EQ(msg_size(MsgType::ReplaceOrder), sizeof(ReplaceOrder));
    EXPECT_EQ(msg_size(MsgType::OrderAdded),     sizeof(OrderAdded));
    EXPECT_EQ(msg_size(MsgType::OrderExecuted),  sizeof(OrderExecuted));
    EXPECT_EQ(msg_size(MsgType::OrderCancelled), sizeof(OrderCancelled));
    EXPECT_EQ(msg_size(MsgType::Trade),          sizeof(Trade));
}

// ── Fuzz: random bytes must not crash decode ──────────────────────────────────

RC_GTEST_PROP(Codec, RandomBytesNocrashEnterOrder, (std::vector<uint8_t> bytes)) {
    EnterOrder out;
    // must not crash or read out of bounds
    decode(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size(), out);
}

RC_GTEST_PROP(Codec, RandomBytesNocrashInboundDispatch, (std::vector<uint8_t> bytes)) {
    decode_inbound(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
}
