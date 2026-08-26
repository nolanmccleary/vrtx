#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "pmu.h"


/* Two uncached, JTAG-coherent host regions (everything else -- text/data/bss/
 * stacks/heap -- is Normal-cacheable, so anything the debugger reads/writes must
 * live in one of these). Both are Device-mapped by build_tables().
 *
 *   HOST_SHARED       -> .telemetry, in SDRAM. Bulk benchmark/EDF telemetry,
 *                        written only after SDRAM is calibrated and mapped.
 *   HOST_SHARED_OCRAM -> .host_ocram, in OCRAM. The few globals that must be
 *                        reachable when SDRAM is not: crash records, the
 *                        pre-c_startup boot gate, and the SMP bring-up mailbox. */
#define HOST_SHARED       __attribute__((section(".telemetry"), used))
#define HOST_SHARED_OCRAM __attribute__((section(".host_ocram"), used))


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


#define METRIC_SLOTS  8   /* slots 0..6 used: 0-3 allocbench, 4-5 rmw, 6 matmul.
                             slot 7 now unused (scheduler cost moved to per-CPU
                             g_cpus[].avg_overhead) */


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

/* Like MEASURE_END, but also stores the raw per-iteration delta into `dst` (any
 * lvalue) so the host can plot the full distribution, not just min/max. */
#define MEASURE_END_INTO(id, dst) \
    do { \
        uint32_t _dt_##id = pmu_cycles() - _mt_##id; \
        metric_add((id), _dt_##id); \
        (dst) = _dt_##id; \
    } while (0)


#endif
