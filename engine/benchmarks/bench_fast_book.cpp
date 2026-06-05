#include "event_listener.hpp"
#include "fast_book.hpp"
#include "types.h"
#include "bench_helpers.hpp"
#include <benchmark/benchmark.h>

namespace {

using namespace lob;

class NullListener : public EventListener {
public:
    void on_order_added(const Order &) override {}
    void on_order_filled(OrderId, OrderId, Price, Quantity) override {}
    void on_order_canceled(OrderId) override {}
    void on_order_modified(OrderId, Quantity) override {}
};

static const Price MIN_P{1};
static const Price MAX_P{10000};
static const Price TICK{1};

// Add a limit buy that never crosses.
// Batch-reconstruct to avoid exhausting the pool across the full benchmark run.
static void BM_FastBook_AddLimitBuyNoFill(benchmark::State &state) {
    NullListener listener;
    static const size_t BATCH = 50000;
    OrderId id = 1;
    while (state.KeepRunningBatch(BATCH)) {
        state.PauseTiming();
        FastBook book(&listener, MIN_P, MAX_P, TICK, BATCH);
        state.ResumeTiming();
        for (size_t i = 0; i < BATCH; ++i) {
            book.add(Order(id, Price(100), SequenceNumber(id), Quantity(10), Side::BUY));
            ++id;
        }
    }
}
BENCHMARK(BM_FastBook_AddLimitBuyNoFill)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Each iteration adds a matching buy+sell pair — one fill, pool slot recycles.
static void BM_FastBook_FullFillOneLevel(benchmark::State &state) {
    NullListener listener;
    FastBook book(&listener, MIN_P, MAX_P, TICK, 2);
    OrderId id = 1;
    for (auto _ : state) {
        book.add(Order(id,     Price(100), SequenceNumber(id),     Quantity(10), Side::SELL));
        book.add(Order(id + 1, Price(100), SequenceNumber(id + 1), Quantity(10), Side::BUY));
        id += 2;
    }
}
BENCHMARK(BM_FastBook_FullFillOneLevel)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Incoming buy sweeps N resting sell levels. Parameterised by depth.
static void BM_FastBook_SweepLevels(benchmark::State &state) {
    NullListener listener;
    const int depth = state.range(0);
    FastBook book(&listener, MIN_P, MAX_P, TICK, depth + 1);
    OrderId id = 1;

    for (auto _ : state) {
        state.PauseTiming();
        for (int i = 0; i < depth; ++i) {
            OrderId oid = id++;
            book.add(Order(oid, Price(100 + i), SequenceNumber(oid), Quantity(10), Side::SELL));
        }
        state.ResumeTiming();

        OrderId oid = id++;
        book.add(Order(oid, Price(100 + depth - 1), SequenceNumber(oid),
                       Quantity(10 * depth), Side::BUY));
    }
}
BENCHMARK(BM_FastBook_SweepLevels)
    ->Arg(1)->Arg(4)->Arg(16)->Arg(64)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Cancel an order by ID from a book with N resting orders.
static void BM_FastBook_CancelById(benchmark::State &state) {
    NullListener listener;
    const int depth = state.range(0);
    FastBook book(&listener, MIN_P, MAX_P, TICK, depth);
    OrderId id = 1;
    OrderId batch_start = 0;

    for (auto _ : state) {
        state.PauseTiming();
        for (int i = 0; i < depth - 1; ++i)
            book.cancel(batch_start + i);
        batch_start = id;
        for (int i = 0; i < depth; ++i) {
            OrderId oid = id++;
            book.add(Order(oid, Price(100 + i), SequenceNumber(oid), Quantity(10), Side::BUY));
        }
        OrderId target = id - 1;
        state.ResumeTiming();

        book.cancel(target);
    }
}
BENCHMARK(BM_FastBook_CancelById)
    ->Arg(1)->Arg(16)->Arg(256)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

// Steady-state: fixed-depth two-sided book, alternating cancel+replace each iteration.
static void BM_FastBook_SteadyState(benchmark::State &state) {
    NullListener listener;
    const int depth = state.range(0);
    FastBook book(&listener, MIN_P, MAX_P, TICK, depth * 4);
    OrderId id = 1;

    for (int i = 0; i < depth; ++i) {
        OrderId bid = id++; book.add(Order(bid, Price(99 - i),  SequenceNumber(bid), Quantity(10), Side::BUY));
        OrderId ask = id++; book.add(Order(ask, Price(101 + i), SequenceNumber(ask), Quantity(10), Side::SELL));
    }

    OrderId cancel_id = 1;
    for (auto _ : state) {
        book.cancel(cancel_id);
        Side side = (cancel_id % 2 == 1) ? Side::BUY : Side::SELL;
        uint64_t price = (side == Side::BUY) ? 99 : 101;
        OrderId oid = id++;
        book.add(Order(oid, Price(price), SequenceNumber(oid), Quantity(10), side));
        ++cancel_id;
    }
}
BENCHMARK(BM_FastBook_SteadyState)
    ->Arg(4)->Arg(16)->Arg(64)
    ->Repetitions(30)
    ->ComputeStatistics("p50", bench::percentile<50>)
    ->ComputeStatistics("p95", bench::percentile<95>)
    ->ComputeStatistics("p99", bench::percentile<99>)
    ->ReportAggregatesOnly(true);

} // namespace
