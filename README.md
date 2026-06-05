# LOB Engine & Microstructure Simulator

A low-latency limit order book matching engine (C++) plus a reactive market-microstructure
simulator (Rust) and analysis layer (Python). Built for HFT-grade, rigorously measured
performance and for studying emergent market behavior.

**Status:** Milestone 1 — correct engine (pre-optimization).

## Working style
This is a hand-built learning/portfolio project. Claude Code is configured as a **read-only
thinking partner** by default (plan mode) and will not edit code unless you switch modes and
ask. See the "How we work together" section in `CLAUDE.md`.

## Quickstart
1. Install Claude Code and run `claude` from this directory.
2. It starts in plan mode (read-only). Read `CLAUDE.md` and `docs/LOB_Technical_Specification.md`.
3. Discuss and design freely; you write the engine. Switch to acceptEdits (Shift+Tab) only
   when you want boilerplate/tests generated, then switch back.

## Layout
| Dir         | Purpose                                   | Language |
|-------------|-------------------------------------------|----------|
| `engine/`   | Matching engine (hot path)                | C++      |
| `protocol/` | Binary wire-protocol definitions (truth)  | shared   |
| `sim/`      | Market simulator + trading agents         | Rust     |
| `ingest/`   | Nasdaq ITCH parser + book reconstruction  | Rust     |
| `analysis/` | Stylized-facts battery + studies          | Python   |
| `viz/`      | Real-time book / tape visualization       | TBD      |
| `data/`     | Sample data (gitignored)                  | —        |
| `docs/`     | Specification and design notes            | —        |
