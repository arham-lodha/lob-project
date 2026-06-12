// lob_runner: standalone engine process.
// Reads binary inbound protocol messages from a file, drives FastBook,
// writes binary outbound messages to another file.
//
// Usage: lob_runner <input.bin> <output.bin>
//
// This is the component that M4's simulator will connect to via socket;
// for now the transport is plain files so the integration test needs no IPC.

#include "../include/codec.h"
#include "../include/event_listener.hpp"
#include "../include/fast_book.hpp"
#include "../include/types.h"
#include "messages.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

using namespace lob;
using namespace lob::codec;
using namespace lob::proto;

// ── Protocol EventListener
// ──────────────────────────────────────────────────── Translates FastBook
// events into outbound protocol messages and writes them to the output stream.

class ProtocolWriter : public EventListener {
public:
  explicit ProtocolWriter(std::ostream &out) : out_(out) {}

  void on_order_added(const Order &order) override {
    OrderAdded msg{};
    msg.side     = static_cast<uint8_t>(order.side);
    msg.qty      = order.quantity.quantity;
    msg.out_seq  = ++out_seq_;
    msg.order_id = order.id;
    msg.price    = order.price.price;
    write_msg(msg);
  }

  void on_order_canceled(OrderId order_id, Quantity cancelled_qty, Quantity) override {
    OrderCancelled msg{};
    msg.cancelled_qty = cancelled_qty.quantity;
    msg.out_seq  = ++out_seq_;
    msg.order_id = order_id;
    write_msg(msg);
  }

  void on_order_modified(OrderId order_id, Quantity new_quantity) override {
    // The protocol has no outbound modify message; quantity reductions are
    // visible to downstream consumers via the book snapshot, not the event stream.
    (void)order_id;
    (void)new_quantity;
  }

  void on_order_filled(OrderId aggressor, OrderId passive, Price price,
                       Quantity quantity) override {
    Trade msg{};
    msg.qty          = quantity.quantity;
    msg.out_seq      = ++out_seq_;
    msg.trade_id     = ++trade_id_;
    msg.passive_id   = passive;
    msg.aggressor_id = aggressor;
    msg.price        = price.price;
    write_msg(msg);
  }

private:
  std::ostream &out_;
  uint64_t out_seq_  = 0;
  uint64_t trade_id_ = 0;

  void write_msg(const OutboundMsg &msg) {
    std::byte buf[MAX_MSG_SIZE];
    size_t n = encode(msg, buf);
    out_.write(reinterpret_cast<const char *>(buf),
               static_cast<std::streamsize>(n));
  }
};

// ── Dispatch loop
// ─────────────────────────────────────────────────────────────

static void run(FastBook &book, std::istream &in) {
  while (true) {
    uint8_t type_byte;
    if (!in.read(reinterpret_cast<char *>(&type_byte), 1)) break;

    MsgType type  = static_cast<MsgType>(type_byte);
    size_t  size  = msg_size(type);
    if (size == 0) break;  // unknown type — cannot frame the rest of the stream

    std::byte buf[MAX_MSG_SIZE];
    buf[0] = static_cast<std::byte>(type_byte);
    if (size > 1 && !in.read(reinterpret_cast<char *>(buf + 1), size - 1)) break;

    auto msg = decode_inbound(buf, size);
    if (!msg) continue;

    std::visit([&](auto &m) {
      using T = std::decay_t<decltype(m)>;

      if constexpr (std::is_same_v<T, EnterOrder>) {
        Order order(m.order_id, Price(m.price), m.seq_num,
                    Quantity(m.quantity),
                    m.side == 0 ? Side::BUY : Side::SELL);
        book.add(order);

      } else if constexpr (std::is_same_v<T, CancelOrder>) {
        if (m.quantity == 0) {
          book.cancel(m.order_id);
        } else {
          book.modify(m.order_id, Quantity(m.remaining_qty));
        }

      } else if constexpr (std::is_same_v<T, ReplaceOrder>) {
        // fixture.bin never contains ReplaceOrder (ingest splits it into
        // CancelOrder + EnterOrder), but handle it defensively.
        book.cancel(m.order_id);
      }
    }, *msg);
  }
}

// ── main
// ──────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: lob_runner <input.bin> <output.bin>\n");
    return 1;
  }

  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open input: %s\n", argv[1]);
    return 1;
  }

  std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "cannot open output: %s\n", argv[2]);
    return 1;
  }

  // TODO: choose reasonable defaults or load from a config / env var.
  const Price MIN_PRICE = Price(1);
  const Price MAX_PRICE =
      Price(9'999'999); // covers all ITCH prices ($0.0001 ticks)
  const size_t MAX_ORDERS = 1'000'000;

  ProtocolWriter writer(out);
  FastBook book(&writer, MIN_PRICE, MAX_PRICE, MAX_ORDERS);

  run(book, in);
  return 0;
}
