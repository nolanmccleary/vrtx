#include <stdint.h>

#include "ktrace.h"
#include "pmu.h"
#include "telemetry.h"
#include "thread.h"
#include "tlsf.h"


#define SZ      64
#define K       64
#define ITERS   512

#define WSET_WORDS  2048   /* 8 KB working set -- fits in the A9 32 KB L1 D-cache */
#define WALK_PASSES 64


typedef enum
{
    MALLOC = 0,
    FREE,
    MALLOC_LOADED,
    FREE_LOADED,
    MEMWALK
} malloc_op_e;


static void* load[K];


void allocbench_run(void)
{
    void* p;


    pmu_init();

    telemetry_init();

    telemetry_name(
        MALLOC,
        "malloc"
    );

    telemetry_name(
        FREE,
        "free"
    );

    telemetry_name(
        MALLOC_LOADED,
        "malloc_loaded"
    );

    telemetry_name(
        FREE_LOADED,
        "free_loaded"
    );

    telemetry_name(
        MEMWALK,
        "mem_walk_8k"
    );


    g_telemetry.read_overhead = pmu_calibrate();


    /*
     * Remove interrupt noise from allocator measurements. Interrupts stay masked
     * for the whole benchmark AND across the handoff to edf_run() -- see the note
     * at the end of this function.
     */
    __asm__ __volatile__(
        "cpsid if"
        :
        :
        : "memory"
    );


    /* ---------------------------------------------------------------------
     * Empty heap
     * ------------------------------------------------------------------------- */

    heap_init();


    for (int i = 0; i < ITERS; i++)
    {
        MEASURE_BEGIN(MALLOC);

        p = kMalloc(SZ);

        MEASURE_END(MALLOC);


        MEASURE_BEGIN(FREE);

        kFree(p);

        MEASURE_END(FREE);
    }


    heap_destroy();


    /* ---------------------------------------------------------------------
     * Loaded heap
     * ------------------------------------------------------------------------- */

    heap_init();


    for (int i = 0; i < K; i++)
    {
        load[i] = kMalloc(SZ);
    }


    for (int i = 0; i < ITERS; i++)
    {
        MEASURE_BEGIN(MALLOC_LOADED);

        p = kMalloc(SZ);

        MEASURE_END(MALLOC_LOADED);


        MEASURE_BEGIN(FREE_LOADED);

        kFree(p);

        MEASURE_END(FREE_LOADED);
    }


    /*
     * Restore allocator state before EDF starts.
     */
    for (int i = 0; i < K; i++)
    {
        kFree(load[i]);
    }


    heap_destroy();


    /* ---------------------------------------------------------------------
     * SDRAM access cost (mem_walk_8k)
     *
     * Read-modify-write an 8 KB heap buffer, WALK_PASSES times, one pass per
     * sample. Uncached SDRAM -> every access hits DDR (slow, flat). With the heap
     * marked cacheable (mmu_cache_init) + D-cache on, the buffer fits in L1, so
     * pass 0 is a cold fill and every pass after is L1 hits (fast). The gap is the
     * cache win -- this is the metric that makes the D-cache visible.
     * ------------------------------------------------------------------------- */

    heap_init();

    volatile uint32_t* buf =
        (volatile uint32_t*)kMalloc(WSET_WORDS * sizeof(uint32_t));

    for (int i = 0; i < WSET_WORDS; i++)   /* untimed init */
    {
        buf[i] = (uint32_t)i;
    }

    for (int pass = 0; pass < WALK_PASSES; pass++)
    {
        MEASURE_BEGIN(MEMWALK);

        for (int i = 0; i < WSET_WORDS; i++)
        {
            buf[i] += 1u;
        }

        MEASURE_END(MEMWALK);
    }

    kFree((void*)buf);

    heap_destroy();
    telemetry_done();


    /*
     * OpenOCD hardware breakpoint is installed at this function address.
     */
    KTRACE_ALLOC_DONE();


    /*
     * Hand off to edf_run() with interrupts STILL MASKED. We just heap_destroy()'d,
     * so the scheduler state left over from c_startup's psched_init() (curr_thread,
     * incomingThreads) now dangles into freed heap. If a tick fired here it would
     * run next_thread() (sched_init is still true) and drain that stale
     * incomingThreads pointer -- walking garbage nodes -> data abort. edf_run()
     * rebuilds the scheduler (heap_init + psched_init) and psched_init re-enables
     * interrupts itself, so there is no reason to unmask them here. Do NOT add a
     * cpsie -- that reopens the window this benchmark exposed.
     */
}
