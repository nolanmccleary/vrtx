#include <stdint.h>

#include "ktrace.h"
#include "pmu.h"
#include "telemetry.h"
#include "thread.h"
#include "tlsf.h"
#include "workload_compute.h"


#define SZ      64
#define K       64
#define ITERS   512

#define WSET_WORDS     2048    /* 8 KB working set -- fits in the A9 32 KB L1 D-cache */
#define WALK_PASSES    64
#define WSET256_WORDS  65536   /* 256 KB -- exceeds the 32 KB L1, fits the 512 KB L2 */
#define WALK256_PASSES 16      /* fewer passes: 256 KB is 32x the per-pass work of 8 KB */


typedef enum
{
    MALLOC = 0,
    FREE,
    MALLOC_LOADED,
    FREE_LOADED,
    MEMWALK,
    MEMWALK256,
    MATMUL
} malloc_op_e;


static void* load[K];


/* One read-modify-write sweep over a heap buffer of `words` uint32, `passes` times,
 * timing each pass into metric[slot]. Its own heap_init/heap_destroy (leaves the heap
 * destroyed). noinline so the 8 KB and 256 KB callers share ONE copy -- OCRAM is tight.
 * Uncached SDRAM -> every access hits DDR (flat). Cacheable + a cache big enough for the
 * working set -> pass 0 cold-fills, later passes hit cache (fast); the gap is the win. */
__attribute__((noinline))
static void mem_walk(int slot, int words, int passes)
{
    heap_init();

    volatile uint32_t* buf = (volatile uint32_t*)kMalloc(words * sizeof(uint32_t));

    for (int i = 0; i < words; i++)   /* untimed init */
    {
        buf[i] = (uint32_t)i;
    }

    for (int pass = 0; pass < passes; pass++)
    {
        MEASURE_BEGIN(slot);

        for (int i = 0; i < words; i++)
        {
            buf[i] += 1u;
        }

        MEASURE_END(slot);
    }

    kFree((void*)buf);

    heap_destroy();
}


void allocbench_run(void)
{
    void* p;


    pmu_init();

    /* Slot -> name mapping (host knows it by enum order):
     * malloc, free, malloc_loaded, free_loaded, mem_walk_8k, mem_walk_256k, matmul_32.
     * NOLOAD table: reset every slot before accumulating. */
    for (int i = 0; i < METRIC_SLOTS; i++)
    {
        metric_reset(i);
    }


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
     * SDRAM access cost -- two working-set sizes, one helper (mem_walk, above).
     *   8 KB   fits the 32 KB L1  -> probes the D-cache (cold fill vs warm L1 hits).
     *   256 KB exceeds L1, fits the 512 KB L2 -> probes the L2 (warm cost drops only
     *          if L2 is live). This is the metric the L2 cache actually earns.
     * ------------------------------------------------------------------------- */

    mem_walk(MEMWALK,    WSET_WORDS,    WALK_PASSES);
    mem_walk(MEMWALK256, WSET256_WORDS, WALK256_PASSES);


    /* ---------------------------------------------------------------------
     * Compute cost (matmul_32) -- the third benchmark. Its own heap_init/
     * heap_destroy; leaves the heap destroyed, same as the blocks above.
     * ------------------------------------------------------------------------- */

    compute_bench(MATMUL);


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
