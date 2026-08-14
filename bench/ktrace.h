#ifndef KTRACE_H
#define KTRACE_H


#ifdef MODE_TEST

#include <stdint.h>

#include "pmu.h"
#include "telemetry.h"
#include "thread.h"


/* -------------------------------------------------------------------------
 * Scheduler telemetry
 * ------------------------------------------------------------------------- */

typedef enum
{
    SM_SCHED_ALL = 0,
    SM_DISPATCH  = 1,
    SM_IDLE      = 2
} sched_metrics_e;


/* Per-tick schedule-trace hook, implemented in the EDF workload. Records which
 * task ran this tick; a no-op outside the traced trial. */
void ktrace_edf_tick(thread_t* running);


#define KTRACE_TICK_ENTER()          \
    uint32_t _kt = pmu_cycles();     \
    int _kdisp = 0;                  \
    void* _krun = 0


#define KTRACE_SWITCH_IN(t)          \
    do {                             \
        _krun  = (thread_t*)(t);         \
        _kdisp = 1;                  \
    } while (0)


#define KTRACE_TICK_EXIT()                                               \
    do {                                                                 \
        uint32_t _kd = telem_correct(pmu_cycles() - _kt);                \
                                                                         \
        if (g_telemetry.running)                                         \
        {                                                                \
            metric_update(                                               \
                &g_telemetry.metric[SM_SCHED_ALL],                       \
                _kd                                                      \
            );                                                           \
                                                                         \
            metric_update(                                               \
                &g_telemetry.metric[                                     \
                    _kdisp ? SM_DISPATCH : SM_IDLE                       \
                ],                                                       \
                _kd                                                      \
            );                                                           \
        }                                                                \
                                                                         \
        ktrace_edf_tick(_krun);   /* outside the measured region above */\
    } while (0)


/* -------------------------------------------------------------------------
 * Existing test/status region
 * ------------------------------------------------------------------------- */

extern char _status_base;


#define STATUS_WORD(n) \
    (*((volatile uint32_t*)&_status_base + (n)))


#define VECTOR_FLAG         STATUS_WORD(0)
#define TICK_MIRROR         STATUS_WORD(1)
#define ALLOC_CHECK         STATUS_WORD(2)
#define SDRAM_TEST_RESULT   STATUS_WORD(3)

#define SCHED_COUNT_1       STATUS_WORD(4)
#define SCHED_COUNT_2       STATUS_WORD(5)

#define GENERAL_FLAG        STATUS_WORD(6)

#define NUM_THREADS         STATUS_WORD(7)
#define NUM_RUNNING         STATUS_WORD(8)

#define THREAD_COUNT_1      STATUS_WORD(9)
#define THREAD_COUNT_2      STATUS_WORD(10)
#define THREAD_COUNT_3      STATUS_WORD(11)


#define FLAG_WRITE(reg, val) \
    ((reg) = (val))


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


#define KTRACE_TICK_ENTER()          ((void)0)
#define KTRACE_SWITCH_IN(t)          ((void)0)
#define KTRACE_TICK_EXIT()           ((void)0)

#define FLAG_WRITE(reg, val)         ((void)0)

#define KTRACE_ALLOC_DONE()          ((void)0)
#define KTRACE_EDF_READY()           ((void)0)
#define KTRACE_EDF_DONE()            ((void)0)

#define KTRACE_WAIT_RELEASE()        ((void)0)
#define KTRACE_RELEASE_PENDING()     (0)


#endif

#endif
