#include <stdint.h>

#include "pmu.h"
#include "telemetry.h"
#include "tlsf.h"
#include "workload_rmw.h"


#define RMW_SLOT_8KB    4       /* g_metrics -> "mem_walk_8k"   (test.py) */
#define RMW_SLOT_256KB  5       /* g_metrics -> "mem_walk_256k" (test.py) */

#define RMW_8KB_WORDS    2048   /* 8192 bytes -- fits the 32768-byte L1 */
#define RMW_8KB_PASSES   64
#define RMW_256KB_WORDS  65536  /* 262144 bytes -- exceeds L1, fits the 524288-byte L2 */
#define RMW_256KB_PASSES 16


/* noinline so the two callers share one copy -- OCRAM is tight. Uncached SDRAM:
 * every access hits DDR (flat). Cacheable + a cache big enough for the working
 * set: pass 0 cold-fills, later passes hit cache; the gap is the win. */
__attribute__((noinline))
static void rmw_sweep(int metric_slot, int word_count, int pass_count)
{
    heap_init();

    volatile uint32_t* sweep_words = (volatile uint32_t*)kMalloc(word_count * sizeof(uint32_t));

    for (int word_index = 0; word_index < word_count; word_index++)
    {
        sweep_words[word_index] = (uint32_t)word_index;
    }

    for (int pass_index = 0; pass_index < pass_count; pass_index++)
    {
        MEASURE_BEGIN(metric_slot);

        for (int word_index = 0; word_index < word_count; word_index++)
        {
            sweep_words[word_index] += 1u;
        }

        MEASURE_END(metric_slot);
    }

    kFree((void*)sweep_words);

    heap_destroy();
}


void rmw_run(void)
{
    metric_reset(RMW_SLOT_8KB);
    metric_reset(RMW_SLOT_256KB);

    rmw_sweep(RMW_SLOT_8KB,   RMW_8KB_WORDS,   RMW_8KB_PASSES);
    rmw_sweep(RMW_SLOT_256KB, RMW_256KB_WORDS, RMW_256KB_PASSES);
}
