#include <stdint.h>

#include "ktrace.h"
#include "pmu.h"
#include "preempt_sched.h"
#include "telemetry.h"
#include "tlsf.h"


#define NTASKS       3u
#define CALIB_ITERS  200000u

#define ARRAY_LEN(a) \
    (sizeof(a) / sizeof((a)[0]))


/* -------------------------------------------------------------------------
 * Host-visible experiment state
 * ------------------------------------------------------------------------- */

const uint32_t g_edf_u_values[] =
{
    500,
    700,
    850,
    900,
    950,
    975,
    1000,
    1025,
    1050,
    1100
};


const uint32_t g_edf_u_count =
    ARRAY_LEN(g_edf_u_values);


const uint32_t g_edf_periods[NTASKS] =
{
    40,
    60,
    100
};


volatile uint32_t g_edf_u_index;
volatile uint32_t g_edf_u_permille;

volatile uint32_t g_edf_C[NTASKS];
volatile uint32_t g_edf_done[NTASKS];


static uint32_t g_iters[NTASKS];


/* -------------------------------------------------------------------------
 * Synthetic jobs
 * ------------------------------------------------------------------------- */

__attribute__((noinline, noclone))
static void do_work(uint32_t iters)
{
    for (uint32_t i = 0; i < iters; i++)
    {
        __asm__ __volatile__("");
    }
}


static sys_exit_e job0(void)
{
    do_work(g_iters[0]);

    g_edf_done[0]++;

    return SYS_OK;
}


static sys_exit_e job1(void)
{
    do_work(g_iters[1]);

    g_edf_done[1]++;

    return SYS_OK;
}


static sys_exit_e job2(void)
{
    do_work(g_iters[2]);

    g_edf_done[2]++;

    return SYS_OK;
}


static sys_exit_e (*const JOBS[NTASKS])(void) =
{
    job0,
    job1,
    job2
};


/* -------------------------------------------------------------------------
 * Calibration
 * ------------------------------------------------------------------------- */

static inline uint32_t rd_ticks(void)
{
    return *(volatile uint32_t*)&gTicks;
}


static uint32_t measure_cycles_per_tick(void)
{
    uint32_t tick = rd_ticks();


    /*
     * Synchronize to a tick edge.
     */
    while (rd_ticks() == tick)
    {
    }


    uint32_t start =
        pmu_cycles();


    tick = rd_ticks();


    while (rd_ticks() < tick + 16u)
    {
    }


    return (
        pmu_cycles() - start
    ) / 16u;
}


static uint32_t calibrate_work(void)
{
    __asm__ __volatile__(
        "cpsid i"
        :
        :
        : "memory"
    );


    uint32_t start =
        pmu_cycles();


    do_work(
        CALIB_ITERS
    );


    uint32_t cycles =
        pmu_cycles() - start;


    __asm__ __volatile__(
        "cpsie i"
        :
        :
        : "memory"
    );


    return cycles;
}


/* -------------------------------------------------------------------------
 * Trial setup
 * ------------------------------------------------------------------------- */

static void configure_work(
    uint32_t u_permille,
    uint32_t cycles_per_tick,
    uint32_t calibration_cycles
)
{
    for (uint32_t i = 0; i < NTASKS; i++)
    {
        uint32_t Ci =
            (
                u_permille *
                g_edf_periods[i]
            )
            /
            (
                1000u *
                NTASKS
            );


        if (Ci < 1u)
        {
            Ci = 1u;
        }


        g_edf_C[i] =
            Ci;


        g_iters[i] =
            (uint32_t)(
                (
                    (uint64_t)Ci *
                    cycles_per_tick *
                    CALIB_ITERS
                )
                /
                calibration_cycles
            );
    }
}


static void reset_trial(void)
{
    gTicks = 0u;
    gMissedDeadlines = 0u;


    for (uint32_t i = 0; i < NTASKS; i++)
    {
        g_edf_done[i] = 0u;
    }


    metric_reset(
        &g_telemetry.metric[SM_SCHED_ALL]
    );

    metric_reset(
        &g_telemetry.metric[SM_DISPATCH]
    );

    metric_reset(
        &g_telemetry.metric[SM_IDLE]
    );
}


/* -------------------------------------------------------------------------
 * Benchmark
 * ------------------------------------------------------------------------- */

void edf_run(void)
{
    pmu_init();


    telemetry_init();

    telemetry_name(
        SM_SCHED_ALL,
        "sched_all"
    );

    telemetry_name(
        SM_DISPATCH,
        "dispatch"
    );

    telemetry_name(
        SM_IDLE,
        "idle"
    );


    g_telemetry.read_overhead =
        pmu_calibrate();


    /*
     * allocbench destroyed its heap before returning.
     */
    heap_init();


    psched_init();


    uint32_t cycles_per_tick =
        measure_cycles_per_tick();


    uint32_t calibration_cycles =
        calibrate_work();


    for (
        uint32_t trial = 0;
        trial < g_edf_u_count;
        trial++
    )
    {
        /*
         * Trial construction must not race the scheduler.
         */
        __asm__ __volatile__(
            "cpsid i"
            :
            :
            : "memory"
        );


        g_edf_u_index =
            trial;


        g_edf_u_permille =
            g_edf_u_values[trial];


        reset_trial();


        configure_work(
            g_edf_u_permille,
            cycles_per_tick,
            calibration_cycles
        );


        g_test_release =
            0u;


        for (uint32_t i = 0; i < NTASKS; i++)
        {
            add_thread(
                JOBS[i],
                g_edf_periods[i],
                PERIODIC
            );
        }


        __asm__ __volatile__(
            "dmb sy"
            :
            :
            : "memory"
        );


        __asm__ __volatile__(
            "cpsie i"
            :
            :
            : "memory"
        );


        /*
         * Python has a hardware breakpoint installed here.
         *
         * Reaching it means this trial is fully configured.
         */
        KTRACE_EDF_READY();


        /*
         * Main stays here while scheduler IRQs execute the EDF workload.
         *
         * Python:
         *
         *     resumes after the breakpoint
         *     waits 12 seconds
         *     halts target
         *     samples state
         *     writes g_test_release = 1
         *     resumes
         */
        KTRACE_WAIT_RELEASE();


        /*
         * Remove this trial before constructing the next one.
         */
        __asm__ __volatile__(
            "cpsid i"
            :
            :
            : "memory"
        );


        psched_clear_threads();


        __asm__ __volatile__(
            "cpsie i"
            :
            :
            : "memory"
        );
    }


    telemetry_done();


    psched_deinit();


    heap_destroy();


    /*
     * Final target -> host event.
     */
    KTRACE_EDF_DONE();
}
