#pragma once
// Protocol codec: encode outbound messages and decode inbound messages.
// All messages are fixed-size, little-endian (host byte order on x86/ARM).
// No allocation; all functions operate on caller-provided buffers.
//
// Decode contract: returns false if buf is too short or msg_type doesn't match.
// Encode contract: buf must be at least sizeof(Msg) bytes.

#include "messages.h"
#include <cstddef>
#include <cstring>
#include <optional>
#include <variant>

namespace lob {
namespace codec {

using namespace proto;

// Maximum size of any single message (for buffer sizing).
constexpr size_t MAX_MSG_SIZE = 48;

// ── Inbound decode ────────────────────────────────────────────────────────────

// Peek at the first byte to determine the message type.
// Returns MsgType(0) if buf is empty.
inline MsgType peek_type(const std::byte* buf, size_t len) {
    if (len == 0) return MsgType(0);
    return static_cast<MsgType>(static_cast<uint8_t>(buf[0]));
}

// Returns the fixed message size for a given type, or 0 if unknown.
size_t msg_size(MsgType t);

bool decode(const std::byte* buf, size_t len, EnterOrder& out);
bool decode(const std::byte* buf, size_t len, CancelOrder& out);
bool decode(const std::byte* buf, size_t len, ReplaceOrder& out);

using InboundMsg = std::variant<EnterOrder, CancelOrder, ReplaceOrder>;

// Dispatch on msg_type byte and decode into the appropriate variant.
// Returns nullopt if the type is unknown or the buffer is too short.
std::optional<InboundMsg> decode_inbound(const std::byte* buf, size_t len);

// ── Outbound encode ───────────────────────────────────────────────────────────

// Write msg into buf.  buf must have at least sizeof(Msg) bytes.
// Returns the number of bytes written.
size_t encode(const OrderAdded& msg, std::byte* buf);
size_t encode(const OrderExecuted& msg, std::byte* buf);
size_t encode(const OrderCancelled& msg, std::byte* buf);
size_t encode(const Trade& msg, std::byte* buf);

using OutboundMsg = std::variant<OrderAdded, OrderExecuted, OrderCancelled, Trade>;

// Dispatch encode via std::visit.
size_t encode(const OutboundMsg& msg, std::byte* buf);

} // namespace codec
} // namespace lob
