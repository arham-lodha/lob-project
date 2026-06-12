#include "ref_book.hpp"
#include "types.h"
#include "event_listener.hpp"
#include "bench_helpers.hpp"
#include <benchmark/benchmark.h>

namespace {

class NullListener : public lob::EventListener {
public:
    void on_order_added(const lob::Order &) override {}
    void on_order_filled(lob::OrderId, lob::OrderId, lob::Price, lob::Quantity) override {}
    void on_order_canceled(lob::OrderId, lob::Quantity, lob::Quantity) override {}
    void on_order_modified(lob::OrderId, lob::Quantity) override {}
};

// Add a limit buy that never crosses — measures pure insert cost.
static void BM_AddLimitBuyNoFill(benchmark::State &state) {
    NullListener listener;
    lob::RefBook book(&listener);
    lob::OrderId id = 1;

    for (auto _ : state) {
        book.add(lob::Order(id, lob::Price(100), lob::SequenceNumber(id), lob::Quantity(10), lob::Side::BUY));
        ++id;
    }
}
BENCHMARK(BM_AddLimitBuyNoFill)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Each iteration adds a matching buy+sell pair — one fill, book stays empty.
static void BM_FullFillOneLevel(benchmark::State &state) {
    NullListener listener;
    lob::OrderId id = 1;

    for (auto _ : state) {
        lob::RefBook book(&listener);
        book.add(lob::Order(id,     lob::Price(100), lob::SequenceNumber(id),     lob::Quantity(10), lob::Side::SELL));
        book.add(lob::Order(id + 1, lob::Price(100), lob::SequenceNumber(id + 1), lob::Quantity(10), lob::Side::BUY));
        id += 2;
    }
}
BENCHMARK(BM_FullFillOneLevel)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Incoming buy sweeps N resting sell levels. Parameterised by depth.
// Uses KeepRunningBatch to amortize PauseTiming overhead across BATCH operations.
// Price space is split into BATCH non-overlapping bands of depth prices each.
static void BM_SweepLevels(benchmark::State &state) {
    NullListener listener;
    const int depth = state.range(0);
    const int BATCH = (10000 - 100) / depth;
    lob::RefBook book(&listener);
    lob::OrderId id = 1;

    while (state.KeepRunningBatch(BATCH)) {
        state.PauseTiming();
        for (int b = 0; b < BATCH; ++b) {
            for (int i = 0; i < depth; ++i) {
                lob::OrderId oid = id++;
                book.add(lob::Order(oid, lob::Price(100 + b * depth + i),
                                    lob::SequenceNumber(oid), lob::Quantity(10), lob::Side::SELL));
            }
        }
        state.ResumeTiming();
        for (int b = 0; b < BATCH; ++b) {
            lob::OrderId oid = id++;
            book.add(lob::Order(oid, lob::Price(100 + b * depth + depth - 1),
                                lob::SequenceNumber(oid), lob::Quantity(10 * depth), lob::Side::BUY));
        }
    }
}
BENCHMARK(BM_SweepLevels)
    ->Arg(1)->Arg(4)->Arg(16)->Arg(64)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Cancel an order by ID from a book with N resting orders.
// Each batch pre-loads depth orders (paused) then cancels all depth (timed).
// Setup:timed ratio is ~1:1 at all depths, keeping total runtime proportional to min_time.
static void BM_CancelById(benchmark::State &state) {
    NullListener listener;
    const int depth = state.range(0);
    lob::RefBook book(&listener);
    lob::OrderId id = 1;

    while (state.KeepRunningBatch(depth)) {
        state.PauseTiming();
        lob::OrderId cancel_start = id;
        for (int i = 0; i < depth; ++i) {
            lob::OrderId oid = id++;
            book.add(lob::Order(oid, lob::Price(100 + i), lob::SequenceNumber(oid),
                                lob::Quantity(10), lob::Side::BUY));
        }
        state.ResumeTiming();
        for (int i = 0; i < depth; ++i)
            book.cancel(cancel_start + i);
    }
}
BENCHMARK(BM_CancelById)
    ->Arg(1)->Arg(16)->Arg(256)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Steady-state: maintain a two-sided book with fixed depth, alternating adds and cancels.
// Closest to a real workload — most orders rest, few fill.
static void BM_SteadyState(benchmark::State &state) {
    NullListener listener;
    const int depth = state.range(0);
    lob::RefBook book(&listener);
    lob::OrderId id = 1;

    for (int i = 0; i < depth; ++i) {
        lob::OrderId bid = id++; book.add(lob::Order(bid, lob::Price(99 - i),  lob::SequenceNumber(bid), lob::Quantity(10), lob::Side::BUY));
        lob::OrderId ask = id++; book.add(lob::Order(ask, lob::Price(101 + i), lob::SequenceNumber(ask), lob::Quantity(10), lob::Side::SELL));
    }

    lob::OrderId cancel_id = 1;
    for (auto _ : state) {
        book.cancel(cancel_id);
        lob::Side side = (cancel_id % 2 == 1) ? lob::Side::BUY : lob::Side::SELL;
        uint64_t price = (side == lob::Side::BUY) ? 99 : 101;
        lob::OrderId oid = id++;
        book.add(lob::Order(oid, lob::Price(price), lob::SequenceNumber(oid),
                            lob::Quantity(10), side));
        ++cancel_id;
    }
}
BENCHMARK(BM_SteadyState)
    ->Arg(4)->Arg(16)->Arg(64)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

} // namespace
