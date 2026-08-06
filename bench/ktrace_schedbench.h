#ifndef __KTRACE_SCHEDBENCH_H__
#define __KTRACE_SCHEDBENCH_H__

#include <stdint.h>
#include "pmu.h"
#include "telemetry.h"

/*
 * Scheduler-benchmark instrumentation. Force-included into every TU (Makefile:
 * -include this) so it wins over kernel/ktrace.h's no-op defaults and turns the
 * next_thread() hook points into real measurement:
 *
 *   sched_all = full next_thread() duration per tick (cycles)
 *   dispatch  = ticks where a real task was selected (KTRACE_SWITCH_IN fired)
 *   idle      = ticks with no ready task (fell through to main)
 *
 * The [enter, exit] bracket spans next_thread's body (heap ops + decision + the SYS
 * sp swap) — the scheduler-algorithm cost — but not the fixed IRQ save/restore, which
 * belongs to interrupt latency (a later metric). read overhead is subtracted; the
 * hist_record runs after the closing timestamp so it is outside the measured region.
 */

enum { SM_SCHED_ALL = 0, SM_DISPATCH = 1, SM_IDLE = 2 };

#define KTRACE_TICK_ENTER() \
    uint32_t _kt = pmu_cycles(); int _kdisp = 0

#define KTRACE_SWITCH_IN(t) \
    do { (void)(t); _kdisp = 1; } while (0)

#define KTRACE_TICK_EXIT()                                                     \
    do {                                                                       \
        uint32_t _kd = telem_correct(pmu_cycles() - _kt);                      \
        hist_record(&g_telemetry.metric[SM_SCHED_ALL].h, _kd);                 \
        hist_record(&g_telemetry.metric[_kdisp ? SM_DISPATCH : SM_IDLE].h, _kd); \
    } while (0)

#endif
