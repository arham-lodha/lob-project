# protocol/
Single source of truth for the binary wire-protocol message layouts.
Inbound (order entry, OUCH-like) and outbound (market data, ITCH-like). Fixed-size,
little-endian, sequence-numbered. C++ and Rust mirror/generate from here.
