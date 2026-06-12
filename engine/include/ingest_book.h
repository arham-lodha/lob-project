#pragma once
// Order book oracle for ITCH fixture generation.
// This is NOT the matching engine — it reconstructs book state from ITCH events
// to produce fixture files for integration testing of the C++ engine.

#include "messages.h"
#include <cstdint>
#include <iosfwd>
#include <map>
#include <unordered_map>
#include <vector>

namespace lob::ingest {

struct LiveOrder {
    uint64_t order_ref;
    uint64_t price;   // widened ITCH tick (raw uint32 → uint64)
    uint32_t shares;
    uint8_t  side;    // 'B' or 'S'
};

class IngestBook {
public:
    bool is_tracked(uint64_t order_ref) const;

    // Handlers write EnterOrder / CancelOrder messages to fixture_out.
    void on_add    (uint64_t ref, uint8_t side, uint32_t shares,
                    uint32_t price, std::ostream& fixture_out);
    void on_cancel (uint64_t ref, uint32_t cancelled_shares,
                    std::ostream& fixture_out);
    void on_delete (uint64_t ref, std::ostream& fixture_out);
    void on_replace(uint64_t orig, uint64_t neo,
                    uint32_t shares, uint32_t price,
                    std::ostream& fixture_out);

    // Execution events record fills internally; they do NOT write to fixture_out.
    void on_executed      (uint64_t ref, uint32_t exec_shares, uint64_t match_num);
    void on_executed_price(uint64_t ref, uint32_t exec_shares, uint64_t match_num,
                           uint32_t price);

    // Write accumulated fill records (proto::Trade, 48 bytes each) to out.
    void write_fills(std::ostream& out) const;

    // Write top-10 bid + top-10 ask snapshot to out.
    // Per level: [side:u8][price:u64 LE][qty:u32 LE] = 13 bytes.
    void write_snapshot(std::ostream& out) const;

private:
    std::unordered_map<uint64_t, LiveOrder> live_;
    std::map<uint64_t, uint32_t> bids_;   // price → total qty, best = highest
    std::map<uint64_t, uint32_t> asks_;   // price → total qty, best = lowest
    std::vector<proto::Trade>    fills_;

    uint64_t seq_num_ = 0;   // stamps inbound fixture messages
    uint64_t out_seq_ = 0;   // stamps outbound fill records

    void emit_enter (const LiveOrder& o, std::ostream& out);
    void emit_cancel(uint64_t order_ref, uint32_t qty, uint32_t remaining, std::ostream& out);

    void add_to_level     (uint8_t side, uint64_t price, uint32_t qty);
    void remove_from_level(uint8_t side, uint64_t price, uint32_t qty);

    void record_fill(uint64_t passive_ref, uint64_t trade_price,
                     uint32_t exec_shares, uint64_t match_num);
};

} // namespace lob::ingest
