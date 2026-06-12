#include "ingest_book.h"
#include <algorithm>
#include <cstring>
#include <ostream>

namespace lob::ingest {

// ── Private helpers ───────────────────────────────────────────────────────────

void IngestBook::emit_enter(const LiveOrder& o, std::ostream& out) {
    proto::EnterOrder m{};
    m.side     = (o.side == 'B') ? 0u : 1u;
    m.quantity = o.shares;
    m.order_id = o.order_ref;
    m.price    = o.price;
    m.seq_num  = ++seq_num_;
    out.write(reinterpret_cast<const char*>(&m), sizeof(m));
}

void IngestBook::emit_cancel(uint64_t order_ref, uint32_t qty, uint32_t remaining, std::ostream& out) {
    proto::CancelOrder m{};
    m.quantity      = qty;
    m.remaining_qty = remaining;
    m.order_id      = order_ref;
    m.seq_num       = ++seq_num_;
    out.write(reinterpret_cast<const char*>(&m), sizeof(m));
}

void IngestBook::add_to_level(uint8_t side, uint64_t price, uint32_t qty) {
    (side == 'B' ? bids_ : asks_)[price] += qty;
}

void IngestBook::remove_from_level(uint8_t side, uint64_t price, uint32_t qty) {
    auto& m = (side == 'B') ? bids_ : asks_;
    auto it = m.find(price);
    if (it == m.end()) return;
    if (qty >= it->second)
        m.erase(it);
    else
        it->second -= qty;
}

void IngestBook::record_fill(uint64_t passive_ref, uint64_t trade_price,
                              uint32_t exec_shares, uint64_t match_num) {
    proto::Trade t{};
    t.qty          = exec_shares;
    t.out_seq      = ++out_seq_;
    t.trade_id     = match_num;
    t.passive_id   = passive_ref;
    t.aggressor_id = 0;  // ITCH 'E' doesn't carry the aggressor ref
    t.price        = trade_price;
    fills_.push_back(t);
}

// ── Public interface ──────────────────────────────────────────────────────────

bool IngestBook::is_tracked(uint64_t order_ref) const {
    return live_.count(order_ref) > 0;
}

void IngestBook::on_add(uint64_t ref, uint8_t side, uint32_t shares,
                         uint32_t price_raw, std::ostream& fixture_out) {
    uint64_t price = price_raw;
    LiveOrder o{ref, price, shares, side};
    live_[ref] = o;
    add_to_level(side, price, shares);
    emit_enter(o, fixture_out);
}

void IngestBook::on_executed(uint64_t ref, uint32_t exec_shares, uint64_t match_num) {
    auto it = live_.find(ref);
    if (it == live_.end()) return;
    LiveOrder& o = it->second;

    record_fill(ref, o.price, exec_shares, match_num);
    remove_from_level(o.side, o.price, exec_shares);

    if (exec_shares >= o.shares)
        live_.erase(it);
    else
        o.shares -= exec_shares;
}

void IngestBook::on_executed_price(uint64_t ref, uint32_t exec_shares,
                                    uint64_t match_num, uint32_t price_raw) {
    auto it = live_.find(ref);
    if (it == live_.end()) return;
    LiveOrder& o = it->second;

    // Fill price is the given execution price; level adjustment uses the order's limit price.
    record_fill(ref, uint64_t{price_raw}, exec_shares, match_num);
    remove_from_level(o.side, o.price, exec_shares);

    if (exec_shares >= o.shares)
        live_.erase(it);
    else
        o.shares -= exec_shares;
}

void IngestBook::on_cancel(uint64_t ref, uint32_t cancelled_shares,
                            std::ostream& fixture_out) {
    auto it = live_.find(ref);
    if (it == live_.end()) return;
    LiveOrder& o = it->second;

    uint32_t actual = std::min(cancelled_shares, o.shares);
    remove_from_level(o.side, o.price, actual);
    o.shares -= actual;
    emit_cancel(ref, actual, o.shares, fixture_out);

    if (o.shares == 0) live_.erase(it);
}

void IngestBook::on_delete(uint64_t ref, std::ostream& fixture_out) {
    auto it = live_.find(ref);
    if (it == live_.end()) return;
    LiveOrder& o = it->second;

    remove_from_level(o.side, o.price, o.shares);
    emit_cancel(ref, 0, 0, fixture_out);   // qty=0 = cancel-all
    live_.erase(it);
}

void IngestBook::on_replace(uint64_t orig, uint64_t neo,
                             uint32_t shares, uint32_t price_raw,
                             std::ostream& fixture_out) {
    auto it = live_.find(orig);
    if (it == live_.end()) return;

    // Inherit side from the original order before erasing it.
    uint8_t side = it->second.side;
    remove_from_level(side, it->second.price, it->second.shares);
    emit_cancel(orig, 0, 0, fixture_out);
    live_.erase(it);

    uint64_t price = price_raw;
    LiveOrder new_order{neo, price, shares, side};
    live_[neo] = new_order;
    add_to_level(side, price, shares);
    emit_enter(new_order, fixture_out);
}

void IngestBook::write_fills(std::ostream& out) const {
    for (const auto& t : fills_)
        out.write(reinterpret_cast<const char*>(&t), sizeof(t));
}

void IngestBook::write_snapshot(std::ostream& out) const {
    int count = 0;
    for (auto it = bids_.rbegin(); it != bids_.rend() && count < 10; ++it, ++count) {
        uint8_t  side  = 0;
        uint64_t price = it->first;
        uint32_t qty   = it->second;
        out.write(reinterpret_cast<const char*>(&side),  1);
        out.write(reinterpret_cast<const char*>(&price), 8);
        out.write(reinterpret_cast<const char*>(&qty),   4);
    }
    count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < 10; ++it, ++count) {
        uint8_t  side  = 1;
        uint64_t price = it->first;
        uint32_t qty   = it->second;
        out.write(reinterpret_cast<const char*>(&side),  1);
        out.write(reinterpret_cast<const char*>(&price), 8);
        out.write(reinterpret_cast<const char*>(&qty),   4);
    }
}

} // namespace lob::ingest
