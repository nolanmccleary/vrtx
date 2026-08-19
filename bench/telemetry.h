#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "pmu.h"


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


#define METRIC_SLOTS  8   /* allocbench uses 7 (malloc..matmul); headroom for one more */


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
