#ifndef __KTRACE_H__
#define __KTRACE_H__

/*
 * Kernel trace hooks. Real per-tick measurement only in the EDF build
 * (-DMODE_EDF from the Makefile); no-ops otherwise, so the production kernel
 * is unaffected. The [enter, exit] bracket spans next_thread's body:
 *   sched_all = full next_thread() cost per tick; dispatch = ticks that ran a task;
 *   idle = ticks with none. read overhead is subtracted; hist_record runs after the
 *   closing timestamp so it stays outside the measured region.
 */

#ifdef MODE_EDF

#include "pmu.h"
#include "telemetry.h"

enum { SM_SCHED_ALL = 0, SM_DISPATCH = 1, SM_IDLE = 2 };

#define KTRACE_TICK_ENTER() \
    uint32_t _kt = pmu_cycles(); int _kdisp = 0

#define KTRACE_SWITCH_IN(t) \
    do { (void)(t); _kdisp = 1; } while (0)

/* Gate recording on state==RUNNING so the histograms cover exactly the capture
   window: the workload hist_reset()s at run start and telemetry_done()s (state=DONE)
   at RUN_TICKS, so ticks outside [run start, RUN_TICKS] are not counted. The check
   is after the closing timestamp, so it stays outside the measured region. */
#define KTRACE_TICK_EXIT()                                                     \
    do {                                                                       \
        uint32_t _kd = telem_correct(pmu_cycles() - _kt);                      \
        if (g_telemetry.state == TELEM_RUNNING) {                              \
            hist_record(&g_telemetry.metric[SM_SCHED_ALL].h, _kd);             \
            hist_record(&g_telemetry.metric[_kdisp ? SM_DISPATCH : SM_IDLE].h, _kd); \
        }                                                                      \
    } while (0)

#else

#define KTRACE_TICK_ENTER() ((void)0)
#define KTRACE_SWITCH_IN(t) ((void)0)
#define KTRACE_TICK_EXIT()  ((void)0)

#endif

#endif
