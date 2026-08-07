#include <stdint.h>
#include "preempt_sched.h"
#include "thread.h"

/*
 * Bare load test: 6 periodic trivial jobs (U ~ 0.33), no pmu/telemetry. Runs the
 * scheduler under sustained multi-task load with no instrumentation.
 */

#define NTASKS 6
static const uint32_t periods[NTASKS] = { 11, 13, 17, 19, 23, 29 };

static sys_exit_e job(thread_status_e* status)
{
    (void)status;
    return SYS_OK;
}

void stress_run(void)
{
    for (uint32_t i = 0; i < NTASKS; i++)
        add_thread(job, periods[i], PERIODIC);

    for (;;) { }   /* scheduler runs from the tick ISR */
}
