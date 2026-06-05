# engine/ (C++)

Hot-path matching engine.

- **Milestone 1 (complete):** `RefBook` — `std::map`-based reference model, passes all 34 tests. Correctness oracle for Milestone 2.
- **Milestone 2 (complete):** `FastBook` — flat price-level arrays, intrusive doubly-linked lists per level, hierarchical bitset for O(1) best bid/ask, pre-allocated memory pool. Differential-tested against `RefBook` (90/90 tests pass).

Layout: `include/` headers, `src/` sources, `tests/`, `benchmarks/`.

---

## Benchmarks

**Machine:** Apple Silicon (10-core), 4 MiB L2 per core  
**Build:** `cmake -DCMAKE_BUILD_TYPE=Release`, `-O3`  
**Date:** 2026-06-04

```
Benchmark                    RefBook (ns)   FastBook (ns)   Speedup
-----------------------------------------------------------------
AddLimitBuyNoFill                    37              43      0.86x
FullFillOneLevel                    130              65      2.00x
SweepLevels/1                       850             818      1.04x
SweepLevels/4                      1014             955      1.06x
SweepLevels/16                     1712            1372      1.25x
SweepLevels/64                     4568            3103      1.47x
CancelById/1                        853             791      1.08x
CancelById/16                      1482             791      1.87x
CancelById/256                    11183             806     13.87x
SteadyState/4                      45.8            56.2      0.82x
SteadyState/16                     62.9            57.4      1.10x
SteadyState/64                     90.4            56.2      1.61x
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
