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

#### Summary (median, all times ns)

FastBook v1: pointer-based nodes (64 B), `std::unordered_map`, vector-of-vectors bitset (2026-06-05).  
FastBook v2: compact nodes (32 B), flat open-addressing hash map, flat bitset (2026-06-07).

```
Benchmark           RefBook   FB v1   FB v2   v1→v2   v2 vs Ref
----------------------------------------------------------------
AddLimitBuyNoFill      27.1    42.2    10.8    3.91x     2.51x
FullFillOneLevel        130    65.0    38.1    1.71x     3.41x
SweepLevels/1          76.0    46.1    23.3    1.98x     3.26x
SweepLevels/4           278     163    91.6    1.78x     3.04x
SweepLevels/16         1100     616     409    1.51x     2.69x
SweepLevels/64         4465    2427    1533    1.58x     2.91x
CancelById/1        829 (†) 801 (†) 788 (†)     n/a       n/a
CancelById/16           112    78.1    58.7    1.33x     1.91x
CancelById/256         85.8    31.9    12.8    2.49x     6.70x
SteadyState/4          46.0    55.1    22.3    2.47x     2.06x
SteadyState/16         68.1    55.1    22.3    2.47x     3.05x
SteadyState/64         90.5    55.4    22.3    2.48x     4.06x
```

† dominated by `PauseTiming` overhead; not meaningful (see notes).

### Benchmark descriptions

| Benchmark | What it measures |
|---|---|
| **AddLimitBuyNoFill** | Pure insert cost. Adds limit buys at a fixed price with no opposing orders — no matching occurs, every order rests. Stresses the data-structure write path (map insert, level update, bitset set). |
| **FullFillOneLevel** | Single-level fill throughput. Each iteration posts a sell then a crossing buy at the same price — one complete fill, book returns to empty. Stresses the match loop, order removal, and pool recycling. |
| **SweepLevels/N** | Multi-level sweep. N sell orders are pre-loaded at N consecutive price levels (setup, not timed). One large buy order crosses all N levels in a single add call. Measures how fast the engine can walk the book and emit fills across levels. Parameterised: N = 1, 4, 16, 64. |
| **CancelById/N** | Cancel latency by depth. N buy orders are pre-loaded across N price levels (setup, not timed). All N are cancelled in the timed section. Directly measures hash-map lookup + splice cost amortized over depth. Parameterised: N = 1, 16, 256. |
| **SteadyState/N** | Closest to a real workload. Book is seeded with N bids and N asks symmetrically around a spread. Each iteration cancels the oldest resting order and replaces it with a fresh one at the same side's top-of-book price — no fills occur. Measures the combined cancel + add throughput under a live book. Parameterised: N = 4, 16, 64. |

### Notes

**SweepLevels** (v2 vs RefBook: 2.7–3.3x, v1→v2: 1.5–2.0x). FastBook avoids `std::map` traversal (O(log N) per level hop) via flat price-level array + hierarchical bitset for O(1) next-level lookup. The v1→v2 gain comes from 32-byte nodes fitting 2/cache line (vs 1 for 64-byte v1 nodes), reducing fill-loop cache misses. Per-fill cost: v2 ~23 ns, v1 ~46 ns, RefBook ~76 ns.

**CancelById** (v2 vs v1: 1.33x at /16, 2.49x at /256). The `PauseTiming`/`ResumeTiming` pair costs ~800 ns on Apple Silicon's 24 MHz timer, making /1 unmeasurable regardless of implementation. At /16 and /256 the overhead amortizes and real cancel cost emerges. v1→v2 gain is from the flat open-addressing hash map (≤2 cache lines per lookup at 50% load) replacing `std::unordered_map` (chained bucketing, 2–3 cache lines). v2 vs RefBook: 1.91x at /16, 6.70x at /256.

**FullFillOneLevel** (v2 vs RefBook: 3.4x, v1→v2: 1.7x). RefBook allocates per fill; both FastBook versions use a pre-allocated pool. v1→v2 gain from compact nodes and faster hash map on the remove path.

**AddLimitBuyNoFill** (v2 vs RefBook: 2.5x, v1→v2: 3.9x). v1 was *slower* than RefBook here (42 vs 27 ns) because `std::unordered_map` insert + vector-of-vectors bitset set_bit outweighed the map cost. v2 reverses this: the flat hash map insert is cheaper than `std::unordered_map`, and the flat bitset (single allocation, contiguous tier words) is substantially faster.

**SteadyState** (v2 vs RefBook: 2.1–4.1x, v1→v2: 2.5x). v2 is depth-invariant at ~22 ns; v1 was invariant at ~55 ns. The 2.5x v1→v2 gain comes equally from the faster cancel (hash map) and faster add (hash map + flat bitset). RefBook's 46→68→91 ns growth reflects `std::map` node cache pressure worsening with depth.

### Reproduce

```bash
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --target lob_bench -j
./engine/build/lob_bench --benchmark_time_unit=ns
```
