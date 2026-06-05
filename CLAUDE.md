# CLAUDE.md

Project memory for Claude Code. Loaded every session — keep it concise and current.

## How we work together (READ THIS FIRST)
This is a learning and portfolio project. **The human writes the core code.** Your role is
thinking partner and collaborator, NOT author. Concretely:
- **Default to discussion.** Explain tradeoffs, critique the approach, answer questions,
  sketch designs in prose or pseudocode, and flag bugs / edge cases being missed. A Socratic
  angle is welcome when it aids learning.
- **Do not write or edit implementation code unless explicitly asked** — e.g. "write this",
  "implement", "generate", "fill this in". A question or a design discussion is NOT a request
  for code. When unsure whether code is wanted, ask.
- **When code IS requested**, it's usually boilerplate, test scaffolding, repetitive/tedious
  pieces, or one specific scoped function. Produce exactly that and no more.
- Prefer small illustrative snippets or pseudocode in chat over editing files, unless asked
  to edit.
- When proposing a design, give options + reasoning, then let the human decide and implement.
- If the human seems about to make a mistake, say so plainly.

This project defaults to **plan mode** (read-only) via `.claude/settings.json`. To have code
written, switch to acceptEdits (Shift+Tab) for that scoped task, then switch back.

## What this is
A low-latency limit order book (LOB) matching engine plus a reactive market-microstructure
simulator. Two equally weighted goals: (1) HFT-grade engineering depth with rigorously
measured performance, and (2) a research lab for studying emergent market behavior.
Full detail in `docs/LOB_Technical_Specification.md` — read it before any substantial work.

## Architecture (two planes)
- **Hot path (C++):** matching engine + protocol codec. Latency-critical, single-writer,
  deterministic. Lives in `engine/`, `protocol/`.
- **Control/research plane:** simulator + agents (Rust, `sim/`), ITCH ingestion (Rust,
  `ingest/`), analysis (Python, `analysis/`), visualization (`viz/`).
- The two planes communicate **only** over the binary wire protocol (OUCH-like inbound,
  ITCH-like outbound). The source of truth for message layouts is `protocol/`.

## Binding design principles (do not violate without explicit discussion)
1. **Correctness before performance.** No optimization until a correct reference
   implementation passes the full invariant suite. Validate the fast path by differential
   testing against that reference.
2. **Determinism.** Same ordered input => byte-identical output. No wall-clock reads in
   matching logic; time is a sequence number supplied by the input stream.
3. **Single-writer hot path.** The match runs on one core. Parallelism lives only in
   ingest / market-data fan-out, never in the match itself.
4. **No allocation on the hot path.** All order/level memory is pre-allocated and pooled.
   No `new`/`malloc` in steady-state matching.
5. **Measure everything.** Every performance claim is backed by an HdrHistogram with a
   documented methodology (rdtscp timing, coordinated-omission aware, warm cache, pinned
   isolated core). A number we cannot defend is treated as no number.
6. **Plane separation.** Latency concerns never leak into the research plane; research
   conveniences never leak into the hot path.

## Current focus
**Milestone 1 — COMPLETE.** `RefBook` (std::map-based reference model) passes all 34
tests: scenario matching, invariants (conservation, no-cross), and rapidcheck property
tests. Google Benchmark baseline established. `RefBook` is the correctness oracle for
Milestone 2.

**Milestone 2 — fast path.** Replace `RefBook`'s per-level `std::vector` with a flat
array of price levels and an intrusive doubly-linked list per level. Goals: O(1) cancel,
no allocation on the hot path, sub-200 ns median match latency. Validate correctness by
differential testing against `RefBook` (same random input sequence → identical fills and
book state). Do not touch `RefBook`; it remains the ground truth.

## Conventions
- **C++:** C++20, CMake. Tests with GoogleTest (or Catch2) + rapidcheck for property tests.
  clang-format + clang-tidy. Warnings-as-errors on engine code.
- **Rust:** stable toolchain. proptest for property tests. clippy clean.
- **Python:** numpy / pandas / scipy / matplotlib; keep analysis in small scripts or
  notebooks under `analysis/`.
- **Invariants every test asserts:** conservation of shares across a match; no crossed book
  at rest; price-time priority never violated; non-negative quantities; unique order IDs.
- Small, focused commits. Write or update tests alongside code. Ask before adding any new
  dependency.

## Build & test commands (fill in as targets materialize)
- Configure: `cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release`
- Build:     `cmake --build engine/build`
- Test:      `ctest --test-dir engine/build --output-on-failure`
- Bench:     `./engine/build/lob_bench --benchmark_time_unit=ns`
- Rust:      `cargo test` within `sim/` and `ingest/`
(Keep this section accurate as the project grows — Claude Code relies on it.)

## Domain glossary (quick)
- **LOB:** limit order book. **Price-time priority:** match best price first, then earliest.
- **Markout:** signed mid-price drift after a fill; core measure of flow informativeness.
- **OFI:** order-flow imbalance. **Kyle's lambda:** price impact per unit net order flow.
- **ITCH / OUCH:** Nasdaq market-data / order-entry protocols this project mimics.
- **Stylized facts:** statistical regularities of real markets (fat tails, volatility
  clustering, etc.) — the simulator's validation target.

## Key references
- Matching structure: flat array of price levels (O(1) price->level) + intrusive
  doubly-linked list per level (O(1) cancel/modify).
- Market making: Avellaneda-Stoikov. Informed-trading / detection: Kyle (1985), PIN/VPIN.
- Data: Nasdaq TotalView-ITCH 5.0 daily sample files.
