#ifndef KTRACE_H
#define KTRACE_H


#ifdef MODE_TEST

#include <stdint.h>

#include "pmu.h"
#include "telemetry.h"
#include "thread.h"



/* -------------------------------------------------------------------------
 * OpenOCD hardware-breakpoint sites
 *
 * Python installs one hardware breakpoint at each function address.
 *
 * The address itself is the target -> host reason:
 *
 *     ktrace_bp_alloc_done  -> allocator benchmark complete
 *     ktrace_bp_edf_ready   -> current EDF trial configured
 *     ktrace_bp_edf_done    -> complete EDF sweep finished
 * ------------------------------------------------------------------------- */

void ktrace_bp_alloc_done(void);
void ktrace_bp_edf_ready(void);
void ktrace_bp_edf_done(void);


/* -------------------------------------------------------------------------
 * Per-tick scheduler hook (test builds only)
 *
 * next_thread() calls this once per tick with the task about to run. The EDF
 * workload (workload_edf.c) implements it: records the Gantt trace and mirrors
 * the running task's metrics into the uncached host-readable region. Kernel is
 * blind to what it does -- pure test payload behind a MODE_TEST gate.
 * ------------------------------------------------------------------------- */

void ktrace_edf_tick(thread_t* running);

#define KTRACE_TICK_EXIT(running) \
    ktrace_edf_tick(running)


#define KTRACE_ALLOC_DONE() \
    ktrace_bp_alloc_done()

#define KTRACE_EDF_READY() \
    ktrace_bp_edf_ready()

#define KTRACE_EDF_DONE() \
    ktrace_bp_edf_done()


/* -------------------------------------------------------------------------
 * Host -> target EDF release
 *
 * This is actual target state rather than a breakpoint reason.
 *
 * Python leaves it zero while an EDF trial runs. After the measurement
 * window Python halts the target, samples state, writes 1, and resumes.
 * ------------------------------------------------------------------------- */

extern volatile uint32_t g_test_release;


void ktrace_wait_release(void);


#define KTRACE_WAIT_RELEASE() \
    ktrace_wait_release()


#define KTRACE_RELEASE_PENDING() \
    (g_test_release != 0u)


#else



#define KTRACE_ALLOC_DONE()          ((void)0)
#define KTRACE_EDF_READY()           ((void)0)
#define KTRACE_EDF_DONE()            ((void)0)

#define KTRACE_TICK_EXIT(running)    ((void)0)

#define KTRACE_WAIT_RELEASE()        ((void)0)
#define KTRACE_RELEASE_PENDING()     (0)


#endif

#endif
