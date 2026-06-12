#pragma once
// ITCH 5.0 parser — host-endian parsed message types.
// Wire format: 2-byte big-endian length prefix, then message body.
// All multi-byte fields in ITCH are big-endian.

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string_view>
#include <variant>

namespace lob::itch {

// ── Parsed (host-endian) message structs ─────────────────────────────────────
// Offsets below are from byte 0 of the body (byte 0 = msg_type).

// 'A' / 'F'  AddOrder / AddOrderMPID  (body 36 / 40 bytes)
// order_ref@11(8), side@19(1), shares@20(4), stock@24(8), price@32(4), ts@5(6)
struct ParsedAddOrder {
    uint64_t order_ref;
    uint64_t timestamp_ns;
    uint32_t shares;
    uint32_t price;         // raw ITCH ticks ($0.0001 units), widen to u64 for proto
    uint8_t  stock[8];      // right-padded with spaces
    uint8_t  side;          // 'B' or 'S'
};

// 'E' / 'C'  OrderExecuted / OrderExecutedWithPrice  (body 31 / 36 bytes)
// order_ref@11(8), exec_shares@19(4), match_num@23(8), ts@5(6)
// price@32(4) and has_price=true only for 'C'
struct ParsedOrderExecuted {
    uint64_t order_ref;
    uint64_t match_number;
    uint64_t timestamp_ns;
    uint32_t exec_shares;
    uint32_t price;         // valid only when has_price == true
    bool     has_price;
};

// 'X'  OrderCancel  (body 23 bytes)
// order_ref@11(8), cancelled_shares@19(4), ts@5(6)
struct ParsedOrderCancel {
    uint64_t order_ref;
    uint64_t timestamp_ns;
    uint32_t cancelled_shares;
};

// 'D'  OrderDelete  (body 19 bytes)
// order_ref@11(8), ts@5(6)
struct ParsedOrderDelete {
    uint64_t order_ref;
    uint64_t timestamp_ns;
};

// 'U'  OrderReplace  (body 35 bytes)
// orig@11(8), new@19(8), shares@27(4), price@31(4), ts@5(6)
struct ParsedOrderReplace {
    uint64_t orig_order_ref;
    uint64_t new_order_ref;
    uint64_t timestamp_ns;
    uint32_t shares;
    uint32_t price;
};

// Parsed message variant; monostate = unknown/skip type
using ItchMsg = std::variant<
    ParsedAddOrder,
    ParsedOrderExecuted,
    ParsedOrderCancel,
    ParsedOrderDelete,
    ParsedOrderReplace,
    std::monostate
>;

// Read one ITCH 5.0 message from a binary stream.
// Returns nullopt on clean EOF (zero bytes available for length prefix).
// Throws std::runtime_error on truncated or corrupt input.
std::optional<ItchMsg> read_itch_message(std::istream& in);

// True if the ITCH stock[8] field (right-padded with spaces) matches sym.
bool matches_symbol(const uint8_t stock[8], std::string_view sym);

} // namespace lob::itch
