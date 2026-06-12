#pragma once
// Fixed-size, little-endian binary protocol.
// Single source of truth for message layouts — C++ and Rust must stay in sync.
//
// Price encoding: tick values matching Nasdaq ITCH 5.0 fixed-point format.
// ITCH stores prices as uint32 in units of $0.0001 (4 decimal places), e.g.
// $12.3456 → 123456. We widen to uint64 and use the value directly as a tick.
//
// Each message starts with msg_type at byte [0].  The remaining bytes are
// padded so all fields sit at their natural alignment (no compiler-inserted
// padding).  static_asserts below verify the layouts.

#include "message_types.h"
#include <cstdint>

namespace lob {
namespace proto {

// ── Inbound messages (engine receives) ───────────────────────────────────────

// [0]     msg_type  u8   = MsgType::EnterOrder
// [1]     side      u8   0=BUY 1=SELL
// [2-3]   padding   u8[2]
// [4-7]   quantity  u32
// [8-15]  order_id  u64
// [16-23] price     u64  (tick value, see above)
// [24-31] seq_num   u64
struct EnterOrder {
    uint8_t  msg_type  = static_cast<uint8_t>(MsgType::EnterOrder);
    uint8_t  side      = 0;
    uint8_t  padding[2]{};
    uint32_t quantity  = 0;
    uint64_t order_id  = 0;
    uint64_t price     = 0;
    uint64_t seq_num   = 0;
};
static_assert(sizeof(EnterOrder) == 32, "EnterOrder must be 32 bytes");

// [0]     msg_type       u8   = MsgType::CancelOrder
// [1-3]   padding        u8[3]
// [4-7]   quantity       u32  (shares to cancel; 0 = cancel all remaining)
// [8-15]  order_id       u64
// [16-23] seq_num        u64
// [24-27] remaining_qty  u32  (shares still resting after this cancel; 0 if cancel-all)
// [28-31] padding2       u8[4]
struct CancelOrder {
    uint8_t  msg_type       = static_cast<uint8_t>(MsgType::CancelOrder);
    uint8_t  padding[3]{};
    uint32_t quantity       = 0;
    uint64_t order_id       = 0;
    uint64_t seq_num        = 0;
    uint32_t remaining_qty  = 0;
    uint8_t  padding2[4]{};
};
static_assert(sizeof(CancelOrder) == 32, "CancelOrder must be 32 bytes");

// [0]     msg_type   u8   = MsgType::ReplaceOrder
// [1-3]   padding    u8[3]
// [4-7]   new_qty    u32
// [8-15]  order_id   u64
// [16-23] new_price  u64
// [24-31] seq_num    u64
struct ReplaceOrder {
    uint8_t  msg_type  = static_cast<uint8_t>(MsgType::ReplaceOrder);
    uint8_t  padding[3]{};
    uint32_t new_qty   = 0;
    uint64_t order_id  = 0;
    uint64_t new_price = 0;
    uint64_t seq_num   = 0;
};
static_assert(sizeof(ReplaceOrder) == 32, "ReplaceOrder must be 32 bytes");

// ── Outbound messages (engine emits) ─────────────────────────────────────────

// [0]     msg_type  u8   = MsgType::OrderAdded
// [1]     side      u8
// [2-3]   padding   u8[2]
// [4-7]   qty       u32
// [8-15]  out_seq   u64  (monotonically increasing output sequence number)
// [16-23] order_id  u64
// [24-31] price     u64
struct OrderAdded {
    uint8_t  msg_type = static_cast<uint8_t>(MsgType::OrderAdded);
    uint8_t  side     = 0;
    uint8_t  padding[2]{};
    uint32_t qty      = 0;
    uint64_t out_seq  = 0;
    uint64_t order_id = 0;
    uint64_t price    = 0;
};
static_assert(sizeof(OrderAdded) == 32, "OrderAdded must be 32 bytes");

// [0]     msg_type       u8   = MsgType::OrderExecuted
// [1-7]   padding        u8[7]
// [8-15]  out_seq        u64
// [16-23] order_id       u64  (passive / resting order)
// [24-31] aggressor_id   u64  (incoming / active order)
// [32-39] price          u64
// [40-43] exec_qty       u32
// [44-47] remaining_qty  u32
struct OrderExecuted {
    uint8_t  msg_type       = static_cast<uint8_t>(MsgType::OrderExecuted);
    uint8_t  padding[7]{};
    uint64_t out_seq        = 0;
    uint64_t order_id       = 0;
    uint64_t aggressor_id   = 0;
    uint64_t price          = 0;
    uint32_t exec_qty       = 0;
    uint32_t remaining_qty  = 0;
};
static_assert(sizeof(OrderExecuted) == 48, "OrderExecuted must be 48 bytes");

// [0]     msg_type       u8   = MsgType::OrderCancelled
// [1-3]   padding        u8[3]
// [4-7]   cancelled_qty  u32
// [8-15]  out_seq        u64
// [16-23] order_id       u64
struct OrderCancelled {
    uint8_t  msg_type      = static_cast<uint8_t>(MsgType::OrderCancelled);
    uint8_t  padding[3]{};
    uint32_t cancelled_qty = 0;
    uint64_t out_seq       = 0;
    uint64_t order_id      = 0;
};
static_assert(sizeof(OrderCancelled) == 24, "OrderCancelled must be 24 bytes");

// [0]     msg_type      u8   = MsgType::Trade
// [1-3]   padding       u8[3]
// [4-7]   qty           u32
// [8-15]  out_seq       u64
// [16-23] trade_id      u64  (match number, monotonically increasing)
// [24-31] passive_id    u64  (resting order)
// [32-39] aggressor_id  u64  (incoming order)
// [40-47] price         u64
struct Trade {
    uint8_t  msg_type      = static_cast<uint8_t>(MsgType::Trade);
    uint8_t  padding[3]{};
    uint32_t qty           = 0;
    uint64_t out_seq       = 0;
    uint64_t trade_id      = 0;
    uint64_t passive_id    = 0;
    uint64_t aggressor_id  = 0;
    uint64_t price         = 0;
};
static_assert(sizeof(Trade) == 48, "Trade must be 48 bytes");

} // namespace proto
} // namespace lob
