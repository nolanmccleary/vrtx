#include <stdint.h>
#include <stdbool.h>
#include "allocator.h"
#include "preempt_sched.h"
#include "thread.h"
#include "bsp.h"
#include "flags.h"
#include "workload.h"

/*
 * Default workload: the EDF scheduler demo (and the test.py regression baseline).
 *
 * Run-to-completion jobs: each release does one bounded unit of work, bumps its
 * completion counter, then RETURNS. Returning lands on the scheduler's thread_exit
 * trampoline, which marks the task FINISHED so EDF re-arms it (periodic) or reaps
 * it (aperiodic). So THREAD_COUNT_n counts completed jobs, not raw loop iterations.
 *
 * JOB_WORK is the per-job work knob: small enough that a job finishes within its
 * period (else it overruns and never re-releases cleanly).
 */
#define JOB_WORK 200000u

static sys_exit_e pthread1(thread_status_e* status)
{
    (void)status;
    for (volatile uint32_t i = 0; i < JOB_WORK; i++) { }
    THREAD_COUNT_1++;
    return SYS_OK;
}


static sys_exit_e pthread2(thread_status_e* status)
{
    (void)status;
    for (volatile uint32_t i = 0; i < JOB_WORK; i++) { }
    THREAD_COUNT_2++;
    return SYS_OK;
}


static sys_exit_e pthread3(thread_status_e* status)
{
    (void)status;
    for (volatile uint32_t i = 0; i < JOB_WORK; i++) { }
    THREAD_COUNT_3++;
    return SYS_OK;
}


static void demo_run(void)
{
    /* System init (scheduler/GIC/tick) already done by main(); this is pure payload. */
    add_thread(pthread1, 67, PERIODIC);
    add_thread(pthread2, 67, PERIODIC);
    add_thread(pthread3, 69, APERIODIC);

    bsp_sdram_selftest();

    uint32_t* test1 = (uint32_t*)kMalloc(sizeof(uint32_t));
    uint32_t* test2 = (uint32_t*)kMalloc(sizeof(uint32_t));
    uint32_t* test3 = (uint32_t*)kMalloc(sizeof(uint32_t));

    kFree(test1);
    kFree(test2);
    kFree(test3);

    *test3 = 0x67;
    FLAG_WRITE(VECTOR_FLAG, 0x1F);

    while (1)
    {
        FLAG_WRITE(ALLOC_CHECK, *test3);
        FLAG_WRITE(GENERAL_FLAG, 0x69);
    }
}


const workload_t g_workload = { "demo", 0, demo_run };
