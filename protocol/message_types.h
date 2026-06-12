#pragma once
#include <cstdint>

namespace lob {
namespace proto {

enum class MsgType : uint8_t {
    // Inbound (order entry, OUCH-like)
    EnterOrder   = 0x01,
    CancelOrder  = 0x02,
    ReplaceOrder = 0x03,

    // Outbound (market data, ITCH-like)
    OrderAdded     = 0x81,
    OrderExecuted  = 0x82,
    OrderCancelled = 0x83,
    Trade          = 0x84,
};

} // namespace proto
} // namespace lob
