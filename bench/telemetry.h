#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "pmu.h"


/* Place a global in the uncached, JTAG-coherent host_shared region (linker
 * .telemetry). Now that all other OCRAM (text/data/bss/stacks) is Normal-
 * cacheable, anything the debugger reads or writes must be marked HOST_SHARED --
 * either a value written directly here, or a mirror of a cached global that a
 * per-tick commit (ktrace_edf_tick) clones in. */
#define HOST_SHARED __attribute__((section(".telemetry"), used))


/* -------------------------------------------------------------------------
 * Host-readable metric table
 *
 * One cycle-count distribution (min / max / running sum / count) per slot. The
 * table lives in the uncached .telemetry region (see linker .host_shared), so a
 * CPU write is visible to a JTAG phys read with no cache maintenance. NOLOAD:
 * the firmware must metric_reset() each slot before use -- power-on OCRAM is
 * garbage, and min starts at 0xFFFFFFFF.
 * ------------------------------------------------------------------------- */

typedef struct
{
    uint32_t min;
    uint32_t max;
    uint32_t sum;     /* mean = sum / count */
    uint32_t count;
}   metric_t;


#define METRIC_SLOTS  8   /* allocbench uses 0..6 (malloc..matmul); slot 7 = scheduler */

/* g_metrics slot 7: per-tick scheduler cost, bracketed around next_thread. */
#define SCHED_METRIC  7


extern metric_t g_metrics[METRIC_SLOTS];


static inline void metric_reset(int slot)
{
    g_metrics[slot].min   = 0xFFFFFFFFu;
    g_metrics[slot].max   = 0u;
    g_metrics[slot].sum   = 0u;
    g_metrics[slot].count = 0u;
}


static inline void metric_add(int slot, uint32_t cycles)
{
    metric_t* m = &g_metrics[slot];

    if (cycles < m->min) m->min = cycles;
    if (cycles > m->max) m->max = cycles;

    m->sum   += cycles;
    m->count += 1u;
}


/* Bracket a region; MEASURE_END folds the elapsed cycle count into slot `id`. */
#define MEASURE_BEGIN(id) \
    uint32_t _mt_##id = pmu_cycles()

#define MEASURE_END(id) \
    metric_add((id), pmu_cycles() - _mt_##id)


#endif
