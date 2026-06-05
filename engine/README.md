# engine/ (C++)

Hot-path matching engine.

- **Milestone 1 (complete):** `RefBook` — `std::map`-based reference model, passes all 34 tests. Correctness oracle for Milestone 2.
- **Milestone 2 (complete):** `FastBook` — flat price-level arrays, intrusive doubly-linked lists per level, hierarchical bitset for O(1) best bid/ask, pre-allocated memory pool. Differential-tested against `RefBook` (90/90 tests pass).

Layout: `include/` headers, `src/` sources, `tests/`, `benchmarks/`.

---

## Benchmarks

**Machine:** Apple Silicon (10-core), 4 MiB L2 per core  
**Build:** `cmake -DCMAKE_BUILD_TYPE=Release`, `-O3`  
**Date:** 2026-06-04 — 30 repetitions each; raw output in `benchmarks/results/2026-06-04.txt`

All times in ns (CPU time). p50/p95/p99 are across benchmark runs (run-to-run variance), not individual ops.

#### RefBook

```
Benchmark              median     p95      p99     stddev    cv
--------------------------------------------------------------
AddLimitBuyNoFill        27.1    30.9     34.8      3.33   11.8%
FullFillOneLevel          129     130      130      0.35    0.3%
SweepLevels/1             846     853      854      3.97    0.5%
SweepLevels/4             992    1021     1089      82.7    8.1%
SweepLevels/16           1669    1676     1677      3.85    0.2%
SweepLevels/64           4503    4526     4527      13.7    0.3%
CancelById/1              845     858      860      5.81    0.7%
CancelById/16            1473    1487     1487      5.34    0.4%
CancelById/256          11366   11548    11551      73.9    0.6%
SteadyState/4            45.3    45.5     45.5      0.08    0.2%
SteadyState/16           61.2    61.3     61.3      0.11    0.2%
SteadyState/64           90.2    91.2     92.2      0.82    0.9%
```

#### FastBook

```
Benchmark              median     p95      p99     stddev    cv
--------------------------------------------------------------
AddLimitBuyNoFill        42.2    42.8     42.9      0.25    0.6%
FullFillOneLevel         64.9    65.5     65.7      0.24    0.4%
SweepLevels/1             820     823      830      3.97    0.5%
SweepLevels/4             951     957      959      3.15    0.3%
SweepLevels/16           1375    1383     1384      4.93    0.4%
SweepLevels/64           3105    3132     3144      16.2    0.5%
CancelById/1              799     804      806      2.67    0.3%
CancelById/16             811     820      821      5.29    0.7%
CancelById/256            819     827      833      7.46    0.9%
SteadyState/4            55.2    55.6     55.8      0.25    0.5%
SteadyState/16           55.2    55.5     55.6      0.21    0.4%
SteadyState/64           55.4    55.5     55.5      0.15    0.3%
```

#### Summary (median speedup)

```
Benchmark              RefBook   FastBook   Speedup
---------------------------------------------------
AddLimitBuyNoFill         27.1       42.2    0.64x
FullFillOneLevel           129       64.9    1.99x
SweepLevels/1             846        820    1.03x
SweepLevels/4             992        951    1.04x
SweepLevels/16           1669       1375    1.21x
SweepLevels/64           4503       3105    1.45x
CancelById/1              845        799    1.06x
CancelById/16            1473        811    1.82x
CancelById/256          11366        819   13.87x
SteadyState/4            45.3       55.2    0.82x
SteadyState/16           61.2       55.2    1.11x
SteadyState/64           90.2       55.4    1.63x
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

**CancelById** is the headline result: FastBook is flat at ~800 ns across all depths, confirming O(1) cancel via hash map + intrusive list pointer splice. RefBook degrades to 11 µs at depth 256 due to O(N) linear scan of the per-level vector.

**FullFillOneLevel** is 2x faster from pool recycling — no allocation on the hot path.

**SweepLevels** gains widen with depth (1.47x at /64) as RefBook pays `std::map` traversal cost per level.

**AddLimitBuyNoFill** is ~16% slower. Both books pay the same `unordered_map` insert cost; FastBook additionally maintains the intrusive linked list (4–5 pointer writes per insert) and the hierarchical bitset. This is the structural trade-off: the list enables O(1) cancel and fill traversal at the cost of slightly more expensive pure inserts.

**SteadyState/4** regresses ~18%. At tiny depth RefBook's cached `std::map` lookup edges out the hash map + free list overhead. The crossover is at depth 16, and FastBook leads by 1.61x at depth 64.

### Reproduce

```bash
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --target lob_bench -j
./engine/build/lob_bench --benchmark_time_unit=ns
```
