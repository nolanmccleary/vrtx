#include <stdint.h>
#include "cpu.h"
#include "ktrace.h"
#include "pmu.h"
#include "preempt_sched.h"
#include "telemetry.h"
#include "thread.h"
#include "tlsf.h"

#define NTASKS       3u
#define CALIB_ITERS  200000u

#define TRACE_TICKS  2400u          /* per-tick schedule-trace window for the host Gantt; 4 hyperperiods (lcm(40,60,100)=600) */

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


const uint32_t g_edf_u_count = ARRAY_LEN(g_edf_u_values);


const uint32_t g_edf_periods[NTASKS] =
{
    40,
    60,
    100
};


/* EDF test/JTAG state -- host_shared (uncached), read/written directly. These
 * are pure test payload, not perf-critical system data, so no caching wanted. */
HOST_SHARED volatile uint32_t g_edf_u_index;
HOST_SHARED volatile uint32_t g_edf_u_permille;

HOST_SHARED volatile uint32_t g_edf_C[NTASKS];
HOST_SHARED volatile uint32_t g_edf_done[NTASKS];

HOST_SHARED volatile uint8_t  g_sched_trace[NUM_CPUS][TRACE_TICKS];
HOST_SHARED volatile uint32_t g_trace_len[NUM_CPUS];

static uint32_t trace_active;   /* 1 while a trial is running (every trial is traced) */
static uint32_t iters[NTASKS];


/* Per-task metrics mirror. The per-tick hook copies the running task's cached
 * metrics_t here; the region is uncached (.telemetry), so a JTAG phys read at the
 * trial halt sees fresh ci_av/ti_av with no cache maintenance -- the CPU copy reads
 * its own fresh cache and writes memory the host reads directly. Index by task. */
/* MIRRORS: cached, system-owned globals cloned into host_shared every tick by
 * ktrace_edf_tick, so the debugger reads coherent values without the CPU paying
 * for uncached access on the hot paths. */
HOST_SHARED metrics_t g_edf_metrics[NTASKS];   /* <- running->metrics (cached thread_t)  */
HOST_SHARED uint32_t  g_ticks_m[NUM_CPUS];
HOST_SHARED uint32_t  g_misses_m[NUM_CPUS];
HOST_SHARED uint32_t  g_overhead_m[NUM_CPUS];



/* -------------------------------------------------------------------------
 * Synthetic jobs
 * ------------------------------------------------------------------------- */

static uint32_t dummy = 0;
__attribute__((noinline, noclone))
static void do_work(uint32_t iters)
{
    for (uint32_t i = 0; i < iters; i++)
    {
        dummy++;
    }
}


/* Both cores run these same jobs concurrently, but only CPU0's completions are
 * tracked, so guard g_edf_done -- otherwise CPU1's jobs double-count it. */
static sys_exit_e job0(void)
{
    do_work(iters[0]);

    if (curr_core() == CPU0) g_edf_done[0]++;

    return SYS_OK;
}


static sys_exit_e job1(void)
{
    do_work(iters[1]);

    if (curr_core() == CPU0) g_edf_done[1]++;

    return SYS_OK;
}


static sys_exit_e job2(void)
{
    do_work(iters[2]);

    if (curr_core() == CPU0) g_edf_done[2]++;

    return SYS_OK;
}


static sys_exit_e (*const JOBS[NTASKS])(void) =
{
    job0,
    job1,
    job2
};


/* Identify the running thread by its job function; idle (main_thread) -> 3. */
static int trace_idx(thread_t* r)
{
    if (r)
    {
        if (r->func == job0) return 0;
        if (r->func == job1) return 1;
        if (r->func == job2) return 2;
    }

    return 3;
}


/* Per-tick hook (invoked from the scheduler via KTRACE_TICK_EXIT). Records the
 * running task id for the traced trial only; a no-op otherwise. */
void ktrace_edf_tick(thread_t* running)
{
    if (!trace_active) return;

    cpu_core_e core = curr_core();
    int idx = trace_idx(running);

    if (g_trace_len[core] < TRACE_TICKS)
    {
        g_sched_trace[core][g_trace_len[core]++] = (uint8_t)idx;
    }

    if (core == CPU0 && idx < (int)NTASKS)
    {
        g_edf_metrics[idx] = running->metrics;
    }

    g_ticks_m[core]    = g_cpus[core].ticks;
    g_misses_m[core]   = g_cpus[core].missed_deadlines;
    g_overhead_m[core] = g_cpus[core].avg_overhead;
}


/* -------------------------------------------------------------------------
 * Calibration
 * ------------------------------------------------------------------------- */

//TODO: Implement mechanism to read kernel objects safely
// returns current gTicks value
static inline uint32_t rd_ticks(void)
{
    return *(volatile uint32_t*)&g_cpus[curr_core()].ticks;
}


//Calculates avg number of cycles per tick
static uint32_t measure_cycles_per_tick(void)
{
    uint32_t tick = rd_ticks();

    /*
     * Synchronize to a tick edge.
     */
    while (rd_ticks() == tick){}

    uint32_t start = pmu_cycles();
    tick = rd_ticks();


    while (rd_ticks() < tick + 16u){} //16 tick averaging window

    return (pmu_cycles() - start) / 16u;
}


//Runs the calibration burst and returns cycles per do_work iteration (>= 1)
static uint32_t measure_cycles_per_iter(void)
{
    __asm__ __volatile__(
        "cpsid i"
        :
        :
        : "memory"
    );


    uint32_t start = pmu_cycles();

    do_work(CALIB_ITERS);

    uint32_t cycles = pmu_cycles() - start;


    __asm__ __volatile__(
        "cpsie i"
        :
        :
        : "memory"
    );


    return cycles / CALIB_ITERS;   // cycles-per-iter: reciprocal is < 1 and would truncate to 0
}


/* -------------------------------------------------------------------------
 * Trial setup
 * ------------------------------------------------------------------------- */

static void configure_work(uint32_t u_permille, uint32_t cycles_per_tick, uint32_t cycles_per_iter)
{
    for (uint32_t i = 0; i < NTASKS; i++)
    {
        uint32_t Ci_ticks =(u_permille * g_edf_periods[i]) / (1000u * NTASKS);

        if (Ci_ticks < 1u) Ci_ticks = 1u;

        g_edf_C[i] = Ci_ticks;
        iters[i] = (uint32_t)((uint64_t)Ci_ticks * cycles_per_tick / cycles_per_iter);
    }
}


static void reset_trial(void)
{
    g_cpus[curr_core()].ticks = 0u;
    g_cpus[curr_core()].missed_deadlines = 0u;


    trace_active   = 1u;   /* trace every trial; the host reads each one and picks which to plot */

    for (uint32_t c = 0; c < NUM_CPUS; c++)
    {
        g_trace_len[c]         = 0u;
        g_cpus[c].avg_overhead = 0u;   /* fresh per-CPU scheduler-overhead EWMA per trial */
    }


    for (uint32_t i = 0; i < NTASKS; i++)
    {
        g_edf_done[i]    = 0u;
        g_edf_metrics[i] = (metrics_t){0};
    }
}


/* -------------------------------------------------------------------------
 * Benchmark
 * ------------------------------------------------------------------------- */

void edf_run(void)
{
    pmu_init();

    /*
     * allocbench destroyed its heap before returning.
     */
    heap_init();
    psched_init();

    for (uint32_t c = 0; c < NUM_CPUS; c++)
    {
        g_ticks_m[c]    = 0u;
        g_misses_m[c]   = 0u;
        g_overhead_m[c] = 0u;
    }

    for (uint32_t i = 0; i < NTASKS; i++)
    {
        g_edf_metrics[i] = (metrics_t){0};
    }


    uint32_t cycles_per_tick = measure_cycles_per_tick();
    uint32_t cycles_per_iter = measure_cycles_per_iter();


    for (uint32_t trial = 0; trial < g_edf_u_count; trial++)
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

        g_edf_u_index = trial;
        g_edf_u_permille = g_edf_u_values[trial];


        reset_trial();


        configure_work(
            g_edf_u_permille,
            cycles_per_tick,
            cycles_per_iter
        );


        g_test_release = 0u;

        thread_t* handles0 [NTASKS];
        thread_t* handles1 [NTASKS];

        /* Both cores run the same workload concurrently. The allocator mutex makes
         * the shared heap safe, and each core's thread_mutex makes the cross-core
         * push into CPU1's incoming FIFO safe against CPU1's own scheduler. */
        for (uint32_t i = 0; i < NTASKS; i++)
        {
            handles0[i] = add_thread_to_core(CPU0, JOBS[i], g_edf_periods[i], PERIODIC);
            handles1[i] = add_thread_to_core(CPU1, JOBS[i], g_edf_periods[i], PERIODIC);
        }


        __asm__ __volatile__(
            "dmb sy"
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

        __asm__ __volatile__(
            "cpsie i"
            :
            :
            : "memory"
        );


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


        // psched_clear_threads();
        for (uint32_t i = 0; i < NTASKS; i++)
        {
            kill_thread(handles0[i]);
            kill_thread(handles1[i]);
        }


        __asm__ __volatile__(
            "cpsie i"
            :
            :
            : "memory"
        );
    }


    KTRACE_EDF_DONE();

    for (;;) {}
}
