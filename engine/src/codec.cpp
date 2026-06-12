#include "codec.h"
#include <cstring>

namespace lob {
namespace codec {

using namespace proto;

size_t msg_size(MsgType t) {
  switch (t) {
  case MsgType::EnterOrder:
    return sizeof(EnterOrder);
  case MsgType::CancelOrder:
    return sizeof(CancelOrder);
  case MsgType::ReplaceOrder:
    return sizeof(ReplaceOrder);
  case MsgType::OrderAdded:
    return sizeof(OrderAdded);
  case MsgType::OrderExecuted:
    return sizeof(OrderExecuted);
  case MsgType::OrderCancelled:
    return sizeof(OrderCancelled);
  case MsgType::Trade:
    return sizeof(Trade);
  default:
    return 0;
  }
}

// ── Inbound decode
// ────────────────────────────────────────────────────────────

bool decode(const std::byte *buf, size_t len, EnterOrder &out) {
  if (len < sizeof(EnterOrder))
    return false;
  if (static_cast<uint8_t>(buf[0]) != static_cast<uint8_t>(MsgType::EnterOrder))
    return false;
  // TODO: read each field via memcpy to avoid UB (little-endian, same as host
  // on x86/ARM LE)
  std::memcpy(&out, buf, sizeof(EnterOrder));
  return true;
}

bool decode(const std::byte *buf, size_t len, CancelOrder &out) {
  if (len < sizeof(CancelOrder))
    return false;
  if (static_cast<uint8_t>(buf[0]) !=
      static_cast<uint8_t>(MsgType::CancelOrder))
    return false;
  // TODO: field-by-field memcpy
  std::memcpy(&out, buf, sizeof(CancelOrder));
  return true;
}

bool decode(const std::byte *buf, size_t len, ReplaceOrder &out) {
  if (len < sizeof(ReplaceOrder))
    return false;
  if (static_cast<uint8_t>(buf[0]) !=
      static_cast<uint8_t>(MsgType::ReplaceOrder))
    return false;
  // TODO: field-by-field memcpy
  std::memcpy(&out, buf, sizeof(ReplaceOrder));
  return true;
}

std::optional<InboundMsg> decode_inbound(const std::byte *buf, size_t len) {
  if (len == 0)
    return std::nullopt;
  switch (peek_type(buf, len)) {
  case MsgType::EnterOrder: {
    EnterOrder m;
    if (!decode(buf, len, m))
      return std::nullopt;
    return m;
  }
  case MsgType::CancelOrder: {
    CancelOrder m;
    if (!decode(buf, len, m))
      return std::nullopt;
    return m;
  }
  case MsgType::ReplaceOrder: {
    ReplaceOrder m;
    if (!decode(buf, len, m))
      return std::nullopt;
    return m;
  }
  default:
    return std::nullopt;
  }
}

// ── Outbound encode
// ───────────────────────────────────────────────────────────

size_t encode(const OrderAdded &msg, std::byte *buf) {
  // TODO: field-by-field memcpy for correctness on big-endian hosts
  std::memcpy(buf, &msg, sizeof(OrderAdded));
  return sizeof(OrderAdded);
}

size_t encode(const OrderExecuted &msg, std::byte *buf) {
  std::memcpy(buf, &msg, sizeof(OrderExecuted));
  return sizeof(OrderExecuted);
}

size_t encode(const OrderCancelled &msg, std::byte *buf) {
  std::memcpy(buf, &msg, sizeof(OrderCancelled));
  return sizeof(OrderCancelled);
}

size_t encode(const Trade &msg, std::byte *buf) {
  std::memcpy(buf, &msg, sizeof(Trade));
  return sizeof(Trade);
}

size_t encode(const OutboundMsg &msg, std::byte *buf) {
  return std::visit([&](const auto &m) { return encode(m, buf); }, msg);
}

} // namespace codec
} // namespace lob
