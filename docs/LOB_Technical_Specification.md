# Limit Order Book & Market Microstructure Simulator
## Technical Specification

**Version:** 0.1 (draft)
**Status:** Working document — sections marked *(open)* are unresolved design forks.

---

## 1. Overview

A low-latency limit order book (LOB) matching engine paired with a reactive market-microstructure simulator. The matching engine is a deterministic, single-writer hot-path component built for HFT-grade throughput and latency. On top of it sits a control/research plane that populates the book with an ecology of trading agents, replays real historical market data, and measures whether the resulting dynamics reproduce the known statistical regularities of real markets.

The project has two equally weighted goals, and every design decision is judged against them:

1. **Engineering depth** — a matching engine whose performance is real, measured rigorously, and defensible under questioning.
2. **Research interest** — a simulator that turns the engine into a laboratory for studying emergent market behavior.

### 1.1 Non-goals (scope boundaries)

- Not a production exchange: no regulatory compliance, no persistence/recovery guarantees beyond deterministic replay, no real-money connectivity.
- Not multi-venue: a single matching engine instance handles a single symbol. Multi-symbol is handled by running independent instances (see §10).
- Not distributed: the hot path is single-process, single-writer by design. Networking is for the simulator/data layers only.
- Not a prediction product: any ML work (§10) is an experiment inside the simulator, not a deployable signal.

---

## 2. Design Principles

These are binding constraints, not aspirations.

- **Correctness before performance.** No optimization until a correct reference implementation passes the full invariant suite. Performance work is validated by differential testing against that reference.
- **Determinism.** Given the same ordered input stream, the engine produces byte-identical output. This is the foundation of replay, testing, and debugging.
- **Single-writer hot path.** The matching engine runs on one core. Parallelism lives in feeding it (ingest) and consuming from it (market data fan-out), never in the match itself. (LMAX Disruptor model.)
- **No allocation on the hot path.** All memory for orders and price levels is pre-allocated and pooled. Zero `malloc`/`new` during steady-state matching.
- **Measure everything, trust nothing.** Every performance claim is backed by a histogram with a documented methodology. A number we cannot defend is treated as no number.
- **Plane separation.** Hot path (latency-critical, C++) is architecturally isolated from the control/research plane (expressiveness-critical, Rust/Python). They communicate only over the wire protocol.

---

## 3. System Architecture

### 3.1 The two planes

**Hot path (C++):**
- Matching engine (library + standalone runner)
- Wire protocol codec (order entry in, market data out)

**Control / research plane:**
- Market simulator and agent framework (Rust)
- ITCH ingestion / book reconstruction (Rust)
- Statistical analysis and stylized-facts battery (Python)
- Real-time visualization (Python or web)

### 3.2 Data flow

```
                 order-entry msgs                  market-data msgs
  [Simulator] ───────────────────▶ [Matching Engine] ───────────────▶ [Market Data Consumers]
  [ITCH replay] ──────────────────▶     (C++)              │                  ├─ Simulator (agents react)
  [Manual/test client] ───────────▶                        │                  ├─ Analysis logger
                                                            │                  └─ Visualizer
                                                            ▼
                                                    deterministic
                                                    event log (replay)
```

### 3.3 The protocol boundary

The engine speaks a binary message protocol inspired by Nasdaq OUCH (inbound order entry) and ITCH (outbound market data). This boundary is what lets C++ and Rust coexist cleanly without FFI, and it mirrors how real venues actually decouple.

- For **latency benchmarking**, the engine is exercised in-process as a library (no IPC in the measurement) so the number reflects matching cost, not transport.
- For the **simulator and realism work**, the engine runs as a standalone process and the protocol crosses a real transport.
- **Transport:** *(open — see §11)*. Default plan: start with a single-producer/single-consumer shared-memory ring buffer for inbound and a broadcast ring for outbound; fall back to Unix domain sockets if the ring buffer proves to be a time sink before it's needed.

---

## 4. Component Specifications

### 4.1 Matching Engine (C++)

**Order types (phased):**
- Phase 1: limit, market, cancel, cancel/replace (modify)
- Phase 2: immediate-or-cancel (IOC), fill-or-kill (FOK), post-only
- Phase 3: iceberg/reserve, stop & stop-limit, pegged (bid/mid/offer)
- Phase 4: opening/closing auction cross (uncross to the max-volume clearing price)

**Matching policy:**
- Primary: price-time (FIFO) priority.
- Secondary *(stretch)*: pro-rata and price-pro-rata, selectable per build, for comparison studies.

**Core data structures (the heart of the engine):**
- **Price levels as a flat array indexed by tick offset.** Price → level is an O(1) array index, not a tree lookup. Bounds defined by a configured price band; out-of-band prices handled by an overflow map (rare path).
- **Each level holds an intrusive doubly-linked list of resting orders**, preserving time priority by insertion order.
- **Order pool:** orders are pre-allocated objects drawn from a free list; an order carries its own list-node pointers (intrusive), so insertion and unlinking are pointer operations.
- **Order ID → order handle map** for O(1) cancel/modify lookup. *(open: open-addressing hash vs slot-map keyed on a generation-tagged ID.)*
- **Best-bid / best-ask cursors** maintained incrementally to avoid scanning.

**Operation complexity targets:**
- Add limit order: O(1)
- Cancel: O(1)
- Modify: O(1) (cancel + re-add, or in-place if price unchanged)
- Match/execute: O(1) amortized per fill

**Determinism requirements:**
- No wall-clock reads in matching logic; time is a sequence number supplied by the input stream.
- No data-structure iteration order that depends on addresses or hashing nondeterminism in observable output.

**Self-trade prevention** *(Phase 3)*: cancel-oldest / cancel-newest / decrement-and-cancel, configurable.

### 4.2 Wire Protocol

Fixed-size, little-endian, sequence-numbered binary messages. No variable-length parsing on the hot path.

**Inbound (order entry, OUCH-like):**
- `Enter Order` (side, price, qty, order type, client order ID)
- `Cancel Order`
- `Replace Order`

**Outbound (market data, ITCH-like):**
- `Order Added`
- `Order Executed` (partial/full)
- `Order Cancelled`
- `Order Deleted`
- `Trade` (for the print/tape)
- `Book Snapshot` (periodic, for late-joining consumers and viz)

Every outbound message carries a monotonically increasing sequence number; a gap is a hard error (drives replay/recovery semantics).

### 4.3 Market Data Ingestion / Replay

- Parser for **Nasdaq TotalView-ITCH 5.0** sample files (publicly available daily samples).
- Reconstruct the full book from the raw add/execute/cancel/delete message stream.
- **Validation:** reconstructed state must reconcile against periodic snapshots / known invariants; mismatch is a test failure.
- Replay a full historical trading day through the engine as an integration test and as a realism demo.

### 4.4 Simulator (Rust)

**Agent abstraction:** a trait exposing `on_market_data(event) -> Vec<Action>` and `on_fill(...)`. Agents see only what the protocol exposes (no oracle access to the book internals beyond market data).

**Agent types (phased):**
- Zero-intelligence (Farmer–Patelli–Zovko style, budget-constrained random) — baseline.
- Market maker (Avellaneda–Stoikov inventory model).
- Momentum / trend follower.
- Mean-reverter.
- Informed trader with a private signal.
- Noise trader.

**Time & latency model:**
- Discrete-event simulation with a global event queue ordered by timestamp.
- Per-agent configurable reaction latency, so a latency-advantaged agent can be studied explicitly.
- *(open: event-driven vs fixed-step time discretization — §11.)*

**Connection:** drives the engine over the wire protocol as one or more order-entry clients; consumes the market-data stream to feed agents.

### 4.5 Analysis (Python)

A **stylized-facts battery** that quantifies how market-like the simulated output is, with plots placed beside equivalent plots from real ITCH data:
- Heavy-tailed return distribution (tail-exponent estimate).
- Absence of linear autocorrelation in returns.
- Volatility clustering (slow-decaying autocorrelation of absolute/squared returns).
- Long memory in the sign of order flow.
- Concave market impact (test for the square-root law).
- Hump-shaped average book depth profile.

Stack: numpy / pandas (or polars) / matplotlib / scipy.

### 4.6 Visualization

Real-time display of: book depth ladder, a liquidity heatmap over time, the trade tape, and the spread/mid time series. Used both for intuition and as the "unforgettable" demo artifact. *(open: native Python (e.g. a lightweight GUI) vs a small web frontend reading the market-data stream.)*

---

## 5. Performance Targets (SLOs)

Measured on a single isolated, pinned core, warm cache, steady state.

| Metric | Target | Stretch |
|---|---|---|
| Add order latency, p50 | < 200 ns | < 100 ns |
| Add order latency, p99 | < 1 µs | < 400 ns |
| Add order latency, p99.99 | < 5 µs | < 1 µs |
| Throughput, single core | > 5 M ops/s | > 10 M ops/s |

**Measurement methodology (mandatory):**
- Timing via `rdtscp` (TSC), calibrated to ns; never `std::chrono` on the hot path.
- Latencies recorded in an **HdrHistogram** to preserve the tail.
- **Coordinated-omission aware:** drive at a fixed intended rate and measure against the intended send time, not the actual.
- Report the full distribution (p50/p99/p99.9/p99.99/max), never just the mean.
- Document: CPU model, frequency scaling disabled, core isolation, hyperthreading state, compiler + flags, warm vs cold cache.

---

## 6. Correctness & Testing Strategy

- **Invariants** checked continuously in debug builds and in tests:
  - Conservation: total shares are neither created nor destroyed across a match.
  - No crossed book: best bid < best ask at rest.
  - Price-time priority: a fill never skips a higher-priority resting order.
  - Quantity non-negativity; order ID uniqueness.
- **Reference model:** a deliberately simple, obviously-correct matching engine (e.g. `std::map`-based). The fast engine is **differential-tested** against it: identical input streams must produce identical output.
- **Property-based testing** (rapidcheck / proptest): generate random valid order streams, assert invariants and reference-equivalence.
- **Fuzzing** the protocol parser (malformed/adversarial byte streams must never crash or corrupt state).
- **Deterministic replay tests:** record a session, replay it, assert byte-identical output.
- **ITCH replay** of a real trading day as an end-to-end integration test.

---

## 7. Tech Stack & Tooling

**Engine (C++):**
- C++20/23; consider `-fno-exceptions` / `-fno-rtti` on the hot path.
- Build: CMake.
- Test: GoogleTest or Catch2; rapidcheck for property tests.
- Benchmark: Google Benchmark + custom HdrHistogram harness.
- Profiling: `perf`, `cachegrind`, optionally VTune; verify cache behavior empirically.

**Simulator (Rust):**
- Stable Rust; `proptest` for property tests.
- Likely no async runtime needed (single-threaded discrete-event loop); revisit if multi-process.

**Analysis (Python):**
- numpy, pandas/polars, scipy, matplotlib, jupyter.

---

## 8. Repository Layout (proposed)

```
/engine        C++ matching engine (lib + runner + benchmarks + tests)
/protocol      Shared message definitions (single source of truth; codegen for both langs)
/sim           Rust simulator and agent framework
/ingest        Rust ITCH parser + book reconstruction
/analysis      Python notebooks + stylized-facts battery
/viz           Visualization frontend
/data          Sample ITCH files (gitignored; download script provided)
/docs          This spec and design notes
```

The `/protocol` directory is the single source of truth for message layouts; both C++ and Rust generate or hand-mirror from it. Drift here is a class of bug worth designing out early.

---

## 9. Milestones & Acceptance Criteria

| # | Milestone | Done when… |
|---|---|---|
| 1 | **Correct engine** | Limit/market/cancel/modify implemented; reference model exists; full invariant + differential test suite green. |
| 2 | **Fast & measured** | Array-of-levels + intrusive lists + pools in place; benchmark harness produces a defensible latency histogram meeting the §5 targets. |
| 3 | **Protocol + real replay** | Binary protocol implemented both sides; a real ITCH trading day reconstructs and replays through the engine, reconciling against snapshots. |
| 4 | **Simulator** | Rust agent framework drives the engine; zero-intelligence + market-maker agents run; output feeds the analysis layer. |
| 5 | **Experiments & viz** | Stylized-facts battery reproduces ≥3 facts; tick-size and latency-advantage experiments run; live visualization works. |

Each milestone is independently demoable and is, on its own, a legitimate interview conversation.

---

## 10. Stretch Goals / Future Work

- Pro-rata and price-pro-rata matching with a comparative study vs price-time.
- RL agent (execution or market-making) trained **inside** the reactive simulator, benchmarked against Almgren–Chriss (execution) closed form.
- Multi-symbol via instance sharding.
- FIX gateway in front of the binary protocol.
- Flash-crash reproduction and analysis as a dedicated study.
- Cross-language protocol codegen.

---

## 11. Open Questions / Design Forks

1. **Transport for the protocol boundary** — shared-memory ring buffer (fastest, more work) vs Unix domain socket (simpler, good enough until proven otherwise). Leaning: socket first, ring buffer only if/when needed.
2. **Order-ID → handle structure** — open-addressing hash map vs generation-tagged slot map. Affects cancel/modify latency and the ABA-style stale-handle question.
3. **Simulator time model** — pure discrete-event (event queue) vs fixed-step discretization. Event-driven is more realistic for latency studies; fixed-step is simpler to reason about.
4. **Visualization surface** — native Python GUI vs lightweight web frontend reading the market-data feed.
5. **Price band sizing** — the flat price-level array needs a bounded tick range; how wide, and how gracefully to handle the overflow path.
6. **Engine language commitment** — C++ is decided for the hot path; confirm whether any part (e.g. the protocol codec) is worth prototyping in Rust as a learning exercise without compromising milestone 2.
