#include <stdint.h>
#include "pmu.h"
#include "telemetry.h"
#include "allocator.h"
#include "flags.h"

/*
 * Allocator benchmark. Pure payload, no kernel hooks: kMalloc/kFree are called
 * synchronously here, so we time each call directly (interrupts masked to isolate).
 * The allocator is first-fit with a full-list coalescing kFree, so cost is ~O(blocks):
 *   - baseline: malloc/free pairs on an empty heap (1 free block) -> best case ~O(1)
 *   - loaded:   K live blocks, so each malloc walks past them and each free coalesces
 *               the whole list -> ~O(K)
 * heap_init() resets the heap between phases for repeatable state. (Heap lives in
 * SDRAM, so timings include DRAM latency.)
 */

#define BENCH_ID_ALLOCBENCH 2
#define SZ    64          /* block size (bytes) */
#define K     64          /* live blocks for the loaded case */
#define ITERS 512         /* measured malloc/free pairs per phase */

enum { MALLOC = 0, FREE = 1, MALLOC_LOADED = 2, FREE_LOADED = 3 };

static void* load[K];


void allocbench_run(void)
{
    uint32_t ro;
    void* p;

    pmu_init();
    telemetry_init(BENCH_ID_ALLOCBENCH);
    telemetry_metric_name(MALLOC,        "malloc");
    telemetry_metric_name(FREE,          "free");
    telemetry_metric_name(MALLOC_LOADED, "malloc_loaded");
    telemetry_metric_name(FREE_LOADED,   "free_loaded");

    pmu_calibrate(&ro);
    g_telemetry.read_overhead_cyc = ro;

    __asm__ __volatile__("cpsid if" ::: "memory");   /* isolate: no ticks during timing */

    /* --- baseline: malloc/free on an empty heap (1 free block) --- */
    heap_init();
    for (int i = 0; i < ITERS; i++) {
        MEASURE_BEGIN(MALLOC);
        p = kMalloc(SZ);
        MEASURE_END(MALLOC);

        MEASURE_BEGIN(FREE);
        kFree(p);
        MEASURE_END(FREE);
    }

    /* --- loaded: K live blocks, so malloc walks past them / free coalesces ~K --- */
    heap_init();
    for (int i = 0; i < K; i++)
        load[i] = kMalloc(SZ);

    for (int i = 0; i < ITERS; i++) {
        MEASURE_BEGIN(MALLOC_LOADED);
        p = kMalloc(SZ);
        MEASURE_END(MALLOC_LOADED);

        MEASURE_BEGIN(FREE_LOADED);
        kFree(p);
        MEASURE_END(FREE_LOADED);
    }

    telemetry_done();
    FLAG_WRITE(GENERAL_FLAG, 0xA11C);        /* sentinel: bench complete */
    for (;;) { }
}
