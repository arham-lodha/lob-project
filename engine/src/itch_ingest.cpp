#include "ingest_book.h"
#include "itch_parser.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: itch_ingest <itch.bin> <SYMBOL> <out_dir>\n");
        return 1;
    }

    const std::string itch_path = argv[1];
    const std::string symbol    = argv[2];
    const std::filesystem::path out_dir = argv[3];

    std::filesystem::create_directories(out_dir);

    std::ifstream in(itch_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "error: cannot open %s\n", itch_path.c_str());
        return 1;
    }
    // Large read buffer to amortize syscall overhead on multi-GB ITCH files.
    static char io_buf[4 * 1024 * 1024];
    in.rdbuf()->pubsetbuf(io_buf, sizeof(io_buf));

    std::ofstream fix_out  (out_dir / "fixture.bin",                   std::ios::binary | std::ios::trunc);
    std::ofstream fills_out(out_dir / "expected_fills.bin",            std::ios::binary | std::ios::trunc);
    std::ofstream snap_out (out_dir / "expected_book_snapshot.bin",    std::ios::binary | std::ios::trunc);
    if (!fix_out || !fills_out || !snap_out) {
        std::fprintf(stderr, "error: cannot open output files in %s\n", out_dir.c_str());
        return 1;
    }

    lob::ingest::IngestBook book;
    uint64_t total = 0, kept = 0;

    try {
        while (true) {
            auto msg = lob::itch::read_itch_message(in);
            if (!msg) break;
            ++total;

            std::visit([&](auto&& m) {
                using T = std::decay_t<decltype(m)>;

                if constexpr (std::is_same_v<T, lob::itch::ParsedAddOrder>) {
                    if (!lob::itch::matches_symbol(m.stock, symbol)) return;
                    ++kept;
                    book.on_add(m.order_ref, m.side, m.shares, m.price, fix_out);

                } else if constexpr (std::is_same_v<T, lob::itch::ParsedOrderExecuted>) {
                    if (!book.is_tracked(m.order_ref)) return;
                    ++kept;
                    if (m.has_price)
                        book.on_executed_price(m.order_ref, m.exec_shares, m.match_number, m.price);
                    else
                        book.on_executed(m.order_ref, m.exec_shares, m.match_number);

                } else if constexpr (std::is_same_v<T, lob::itch::ParsedOrderCancel>) {
                    if (!book.is_tracked(m.order_ref)) return;
                    ++kept;
                    book.on_cancel(m.order_ref, m.cancelled_shares, fix_out);

                } else if constexpr (std::is_same_v<T, lob::itch::ParsedOrderDelete>) {
                    if (!book.is_tracked(m.order_ref)) return;
                    ++kept;
                    book.on_delete(m.order_ref, fix_out);

                } else if constexpr (std::is_same_v<T, lob::itch::ParsedOrderReplace>) {
                    if (!book.is_tracked(m.orig_order_ref)) return;
                    ++kept;
                    book.on_replace(m.orig_order_ref, m.new_order_ref,
                                    m.shares, m.price, fix_out);

                }
                // std::monostate: unknown type, skip
            }, *msg);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parse error after %llu messages: %s\n",
                     static_cast<unsigned long long>(total), e.what());
        return 1;
    }

    book.write_fills(fills_out);
    book.write_snapshot(snap_out);

    std::fprintf(stderr, "parsed %llu messages, kept %llu for %s\n",
                 static_cast<unsigned long long>(total),
                 static_cast<unsigned long long>(kept),
                 symbol.c_str());
    return 0;
}
