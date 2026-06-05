# engine/ (C++)

Hot-path matching engine.

- **Milestone 1 (complete):** `RefBook` — `std::map`-based reference model, passes all 34 tests. Correctness oracle for Milestone 2.
- **Milestone 2 (complete):** `FastBook` — flat price-level arrays, intrusive doubly-linked lists per level, hierarchical bitset for O(1) best bid/ask, pre-allocated memory pool. Differential-tested against `RefBook` (90/90 tests pass).

Layout: `include/` headers, `src/` sources, `tests/`, `benchmarks/`.

---

## Benchmarks

**Machine:** Apple Silicon (10-core), 4 MiB L2 per core  
**Build:** `cmake -DCMAKE_BUILD_TYPE=Release`, `-O3`  
**Date:** 2026-06-05 — 30 repetitions each; raw output in `benchmarks/results/2026-06-05.txt`

All times in ns (CPU time). p50/p95/p99 are across benchmark runs (run-to-run variance), not individual ops.

#### RefBook

```
Benchmark              median     p95      p99     stddev    cv
--------------------------------------------------------------
AddLimitBuyNoFill        27.8    35.5     36.5      3.04   10.5%
FullFillOneLevel          129     130      130      0.31    0.2%
SweepLevels/1            76.3    76.8     76.8      0.24    0.3%
SweepLevels/4             279     281      285      1.55    0.6%
SweepLevels/16           1110    1117     1120      4.34    0.4%
SweepLevels/64           4497    4523     4524      16.5    0.4%
CancelById/1          829 (†)     834      835      3.03    0.4%
CancelById/16             110     110      111      0.38    0.4%
CancelById/256           85.4    85.7     85.7      0.36    0.4%
SteadyState/4            45.6    45.8     45.8      0.11    0.2%
SteadyState/16           61.0    61.2     61.2      0.08    0.1%
SteadyState/64           89.5    89.8     89.8      0.24    0.3%
```

#### FastBook

```
Benchmark              median     p95      p99     stddev    cv
--------------------------------------------------------------
AddLimitBuyNoFill        42.2    42.2     42.3      0.04    0.1%
FullFillOneLevel         65.0    65.3     65.3      0.12    0.2%
SweepLevels/1            46.1    46.4     46.4      0.14    0.3%
SweepLevels/4             163     164      164      0.40    0.2%
SweepLevels/16            616     619      619      1.48    0.2%
SweepLevels/64           2427    2438     2439      6.15    0.3%
CancelById/1          804 (†)     807      807      2.75    0.3%
CancelById/16            78.3    78.7     78.8      0.22    0.3%
CancelById/256           31.9    31.9     31.9      0.07    0.2%
SteadyState/4            55.1    55.3     55.4      0.12    0.2%
SteadyState/16           55.1    55.3     55.4      0.14    0.2%
SteadyState/64           55.3    55.4     55.4      0.12    0.2%
```

† CancelById/1 times are dominated by `PauseTiming`/`ResumeTiming` overhead (~800 ns per call on Apple Silicon's 24 MHz system timer). The batch size is 1, so overhead is per-op and the actual cancel cost is not measurable with this methodology. CancelById/16 and /256 have meaningful signal.

#### Summary (median speedup)

```
Benchmark              RefBook   FastBook   Speedup
---------------------------------------------------
AddLimitBuyNoFill         27.8       42.2    0.66x
FullFillOneLevel           129       65.0    1.98x
SweepLevels/1            76.3       46.1    1.65x
SweepLevels/4             279        163    1.71x
SweepLevels/16           1110        616    1.80x
SweepLevels/64           4497       2427    1.85x
CancelById/1          829 (†)   804 (†)      n/a
CancelById/16             110       78.3    1.40x
CancelById/256           85.4       31.9    2.68x
SteadyState/4            45.6       55.1    0.83x
SteadyState/16           61.0       55.1    1.11x
SteadyState/64           89.5       55.3    1.62x
```

### Benchmark descriptions

| Benchmark | What it measures |
|---|---|
| **AddLimitBuyNoFill** | Pure insert cost. Adds limit buys at a fixed price with no opposing orders — no matching occurs, every order rests. Stresses the data-structure write path (map insert, level update, bitset set). |
| **FullFillOneLevel** | Single-level fill throughput. Each iteration posts a sell then a crossing buy at the same price — one complete fill, book returns to empty. Stresses the match loop, order removal, and pool recycling. |
| **SweepLevels/N** | Multi-level sweep. N sell orders are pre-loaded at N consecutive price levels (setup, not timed). One large buy order crosses all N levels in a single add call. Measures how fast the engine can walk the book and emit fills across levels. Parameterised: N = 1, 4, 16, 64. |
| **CancelById/N** | Cancel latency by depth. N buy orders are pre-loaded across N price levels (setup, not timed). One cancel by ID is timed. Directly measures the O(1) vs O(N) cancel cost difference between the two implementations. Parameterised: N = 1, 16, 256. |
| **SteadyState/N** | Closest to a real workload. Book is seeded with N bids and N asks symmetrically around a spread. Each iteration cancels the oldest resting order and replaces it with a fresh one at the same side's top-of-book price — no fills occur. Measures the combined cancel + add throughput under a live book. Parameterised: N = 4, 16, 64. |

### Notes

**SweepLevels** is FastBook's clearest win on this benchmark design: 1.65x at depth 1, growing to 1.85x at depth 64. The gain is consistent because the batch design amortizes `PauseTiming` overhead across BATCH operations, isolating the actual sweep cost. FastBook avoids `std::map` traversal (O(log N) per level hop) by using a flat array + hierarchical bitset for O(1) next-level lookup. Per-fill cost: FastBook ~43 ns/fill, RefBook ~68 ns/fill — structural, not depth-dependent.

**CancelById/1** is not meaningful for either implementation: the `PauseTiming`/`ResumeTiming` pair costs ~800 ns on Apple Silicon's 24 MHz system timer, and with batch size 1 this overhead is per-operation. At depth=16 and depth=256 the overhead is amortized (50 ns/op and 3 ns/op respectively) and the real cancel cost emerges. FastBook is 1.40x faster at depth 16 and 2.68x at depth 256, confirming O(1) cancel (hash map lookup + pointer splice) vs. RefBook's O(log N) map lookup.

**FullFillOneLevel** is 2x faster from pool recycling — no allocation on the hot path.

**AddLimitBuyNoFill** is ~52% slower. Both books pay the same `unordered_map` insert cost; FastBook additionally maintains the intrusive linked list (4–5 pointer writes per insert) and the hierarchical bitset. This is the structural trade-off: the list enables O(1) cancel and fill traversal at the cost of slightly more expensive pure inserts.

**SteadyState/4** regresses ~21%. At tiny depth RefBook's cached `std::map` lookup edges out the hash map + free list overhead. The crossover is at depth 16, and FastBook leads by 1.62x at depth 64. FastBook's SteadyState is depth-invariant at ~55 ns because the cancel + add cost doesn't scale with book size (O(1) everything). RefBook's 45→61→90 ns growth reflects increasing `std::map` node cache pressure.

### Reproduce

```bash
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --target lob_bench -j
./engine/build/lob_bench --benchmark_time_unit=ns
```
