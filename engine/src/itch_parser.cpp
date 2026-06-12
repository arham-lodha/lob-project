#include "itch_parser.h"
#include <cstring>
#include <istream>
#include <stdexcept>

namespace lob::itch {

namespace {

// Big-endian reads by offset — safe on unaligned addresses via memcpy.
static uint16_t read_be16(const uint8_t* p) {
    uint16_t v; std::memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}
static uint32_t read_be32(const uint8_t* p) {
    uint32_t v; std::memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}
static uint64_t read_be64(const uint8_t* p) {
    uint64_t v; std::memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}
// 6-byte ITCH timestamp (nanoseconds since midnight)
static uint64_t read_be48(const uint8_t* p) {
    return (uint64_t(p[0]) << 40) | (uint64_t(p[1]) << 32) |
           (uint64_t(p[2]) << 24) | (uint64_t(p[3]) << 16) |
           (uint64_t(p[4]) <<  8) |  uint64_t(p[5]);
}

} // namespace

std::optional<ItchMsg> read_itch_message(std::istream& in) {
    // Read the 2-byte big-endian length prefix.
    uint8_t len_buf[2];
    if (!in.read(reinterpret_cast<char*>(len_buf), 2)) {
        if (in.eof() && in.gcount() == 0) return std::nullopt;
        throw std::runtime_error("truncated ITCH length prefix");
    }
    uint16_t body_len = read_be16(len_buf);
    if (body_len == 0 || body_len > 255)
        throw std::runtime_error("implausible ITCH body length");

    uint8_t buf[256];
    if (!in.read(reinterpret_cast<char*>(buf), body_len))
        throw std::runtime_error("truncated ITCH message body");

    switch (buf[0]) {
    case 'A':   // AddOrder (36 bytes)
    case 'F': { // AddOrderMPID (40 bytes) — mpid ignored
        ParsedAddOrder m{};
        m.order_ref    = read_be64(buf + 11);
        m.timestamp_ns = read_be48(buf + 5);
        m.side         = buf[19];
        m.shares       = read_be32(buf + 20);
        std::memcpy(m.stock, buf + 24, 8);
        m.price        = read_be32(buf + 32);
        return m;
    }
    case 'E': { // OrderExecuted (31 bytes)
        ParsedOrderExecuted m{};
        m.order_ref    = read_be64(buf + 11);
        m.timestamp_ns = read_be48(buf + 5);
        m.exec_shares  = read_be32(buf + 19);
        m.match_number = read_be64(buf + 23);
        m.has_price    = false;
        m.price        = 0;
        return m;
    }
    case 'C': { // OrderExecutedWithPrice (36 bytes)
        ParsedOrderExecuted m{};
        m.order_ref    = read_be64(buf + 11);
        m.timestamp_ns = read_be48(buf + 5);
        m.exec_shares  = read_be32(buf + 19);
        m.match_number = read_be64(buf + 23);
        m.has_price    = true;
        m.price        = read_be32(buf + 32);
        return m;
    }
    case 'X': { // OrderCancel (23 bytes)
        ParsedOrderCancel m{};
        m.order_ref         = read_be64(buf + 11);
        m.timestamp_ns      = read_be48(buf + 5);
        m.cancelled_shares  = read_be32(buf + 19);
        return m;
    }
    case 'D': { // OrderDelete (19 bytes)
        ParsedOrderDelete m{};
        m.order_ref    = read_be64(buf + 11);
        m.timestamp_ns = read_be48(buf + 5);
        return m;
    }
    case 'U': { // OrderReplace (35 bytes)
        ParsedOrderReplace m{};
        m.orig_order_ref = read_be64(buf + 11);
        m.new_order_ref  = read_be64(buf + 19);
        m.timestamp_ns   = read_be48(buf + 5);
        m.shares         = read_be32(buf + 27);
        m.price          = read_be32(buf + 31);
        return m;
    }
    default:
        return std::monostate{};
    }
}

bool matches_symbol(const uint8_t stock[8], std::string_view sym) {
    if (sym.size() > 8) return false;
    for (size_t i = 0; i < sym.size(); ++i)
        if (stock[i] != static_cast<uint8_t>(sym[i])) return false;
    for (size_t i = sym.size(); i < 8; ++i)
        if (stock[i] != ' ') return false;
    return true;
}

} // namespace lob::itch
