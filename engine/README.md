# engine/ (C++)

Hot-path matching engine.

- **Milestone 1 (complete):** `RefBook` — `std::map`-based reference model, passes all 34 tests. Correctness oracle for Milestone 2.
- **Milestone 2 (complete):** `FastBook` — flat price-level arrays, intrusive doubly-linked lists per level, hierarchical bitset for O(1) best bid/ask, pre-allocated memory pool. Differential-tested against `RefBook` (90/90 tests pass).

Layout: `include/` headers, `src/` sources, `tests/`, `benchmarks/`.

---

## Benchmarks

**Machine:** Apple Silicon (10-core), 4 MiB L2 per core  
**Build:** `cmake -DCMAKE_BUILD_TYPE=Release`, `-O3`  
**Date:** 2026-06-07 — 30 repetitions each; raw output in `benchmarks/results/2026-06-07.txt`

All times in ns (wall time). p50/p95/p99 are across benchmark runs (run-to-run variance), not individual ops.

#### RefBook

```
Benchmark              median     p95      p99     stddev    cv
--------------------------------------------------------------
AddLimitBuyNoFill        27.1    43.2     46.6      7.86   26.7%
FullFillOneLevel          130     130      131      0.70    0.5%
SweepLevels/1            76.0    76.5     77.8      0.79    1.0%
SweepLevels/4             278     281      283      1.74    0.6%
SweepLevels/16           1100    1114     1122      21.1    1.9%
SweepLevels/64           4465    4514     4599      48.8    1.1%
CancelById/1          829 (†)     832      861      9.50    1.1%
CancelById/16             112     116      122      8.05    7.1%
CancelById/256           85.8    88.7     88.7      1.28    1.5%
SteadyState/4            46.0    46.3     46.8      0.25    0.5%
SteadyState/16           68.1    68.7     70.6      0.75    1.1%
SteadyState/64           90.5    91.0     91.5      0.38    0.4%
```

#### FastBook

```
Benchmark              median     p95      p99     stddev    cv
--------------------------------------------------------------
AddLimitBuyNoFill        10.8    11.6     11.6      0.30    2.7%
FullFillOneLevel         38.1    39.1     39.5      1.53    4.0%
SweepLevels/1            23.3    23.5     23.6      1.20    5.1%
SweepLevels/4            91.6    93.3     93.3      0.88    1.0%
SweepLevels/16            409     418      424      4.97    1.2%
SweepLevels/64           1533    1556     1559      14.1    0.9%
CancelById/1          788 (†)     797      803      5.60    0.7%
CancelById/16            58.7    60.2     63.5      3.04    5.1%
CancelById/256           12.8    12.9     13.0      0.15    1.2%
SteadyState/4            22.3    22.7     22.9      0.27    1.2%
SteadyState/16           22.3    22.7     22.7      0.18    0.8%
SteadyState/64           22.3    22.5     22.6      0.52    2.3%
```

† CancelById/1 times are dominated by `PauseTiming`/`ResumeTiming` overhead (~800 ns per call on Apple Silicon's 24 MHz system timer). The batch size is 1, so overhead is per-op and the actual cancel cost is not measurable with this methodology. CancelById/16 and /256 have meaningful signal.

#### Summary (median speedup)

```
Benchmark              RefBook   FastBook   Speedup
---------------------------------------------------
AddLimitBuyNoFill         27.1       10.8    2.51x
FullFillOneLevel           130       38.1    3.41x
SweepLevels/1            76.0       23.3    3.26x
SweepLevels/4             278       91.6    3.04x
SweepLevels/16           1100        409    2.69x
SweepLevels/64           4465       1533    2.91x
CancelById/1          829 (†)   788 (†)      n/a
CancelById/16             112       58.7    1.91x
CancelById/256           85.8       12.8    6.70x
SteadyState/4            46.0       22.3    2.06x
SteadyState/16           68.1       22.3    3.05x
SteadyState/64           90.5       22.3    4.06x
```

### Benchmark descriptions

| Benchmark | What it measures |
|---|---|
| **AddLimitBuyNoFill** | Pure insert cost. Adds limit buys at a fixed price with no opposing orders — no matching occurs, every order rests. Stresses the data-structure write path (map insert, level update, bitset set). |
| **FullFillOneLevel** | Single-level fill throughput. Each iteration posts a sell then a crossing buy at the same price — one complete fill, book returns to empty. Stresses the match loop, order removal, and pool recycling. |
| **SweepLevels/N** | Multi-level sweep. N sell orders are pre-loaded at N consecutive price levels (setup, not timed). One large buy order crosses all N levels in a single add call. Measures how fast the engine can walk the book and emit fills across levels. Parameterised: N = 1, 4, 16, 64. |
| **CancelById/N** | Cancel latency by depth. N buy orders are pre-loaded across N price levels (setup, not timed). All N are cancelled in the timed section. Directly measures hash-map lookup + splice cost amortized over depth. Parameterised: N = 1, 16, 256. |
| **SteadyState/N** | Closest to a real workload. Book is seeded with N bids and N asks symmetrically around a spread. Each iteration cancels the oldest resting order and replaces it with a fresh one at the same side's top-of-book price — no fills occur. Measures the combined cancel + add throughput under a live book. Parameterised: N = 4, 16, 64. |

### Notes

**SweepLevels** shows a consistent 2.7–3.3x gain. FastBook avoids `std::map` traversal (O(log N) per level hop) by using a flat array + hierarchical bitset for O(1) next-level lookup. Per-fill cost: FastBook ~23 ns/fill at depth 1, RefBook ~76 ns/fill — structural, not depth-dependent.

**CancelById/1** is not meaningful for either implementation: the `PauseTiming`/`ResumeTiming` pair costs ~800 ns on Apple Silicon's 24 MHz system timer, and with batch size 1 this overhead is per-operation. At depth=16 and depth=256 the overhead is amortized and the real cancel cost emerges. FastBook is 1.91x faster at depth 16 and 6.70x at depth 256, confirming O(1) cancel (flat hash map lookup + index splice) vs. RefBook's O(log N) map erase.

**FullFillOneLevel** is 3.4x faster: pool recycling eliminates allocation, and the flat hash map + compact 32-byte nodes keep the fill loop in cache.

**AddLimitBuyNoFill** is 2.5x faster. The flat open-addressing hash map insert is substantially cheaper than `std::unordered_map`, and the flat HierarchicalBitset (single allocation, contiguous tier words) makes the bitset set-bit path faster than the previous vector-of-vectors layout.

**SteadyState** is depth-invariant at ~22 ns because cancel + add cost is O(1) regardless of book size. RefBook's 46→68→91 ns growth across depths 4/16/64 reflects increasing `std::map` node cache pressure. The 2–4x spread widens with depth for this reason.

### Reproduce

```bash
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --target lob_bench -j
./engine/build/lob_bench --benchmark_time_unit=ns
```
