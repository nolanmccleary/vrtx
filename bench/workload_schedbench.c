#include <stdint.h>
#include "pmu.h"
#include "telemetry.h"
#include "preempt_sched.h"
#include "thread.h"
#include "flags.h"
#include "workload.h"

/*
 * Scheduler benchmark. Pure payload: main() has already brought up the scheduler,
 * GIC, and tick. We enable the PMU, register a task set that forces a dispatch on
 * essentially every tick, and let the kernel's KTRACE hooks accumulate per-tick
 * scheduler cost in the timer ISR. Warm-cache: reset the histograms after a warmup
 * window, then collect a fixed number of samples.
 */

#define BENCH_ID_SCHEDBENCH 1
#define NTASKS         6
#define WARMUP_TICKS   256
/* Capped below the sustained-load abort threshold (a scheduler heap-corruption bug
   surfaces around ~1400-2200 ticks; see notes). 1000 samples is a solid distribution. */
#define TARGET_SAMPLES 1000

/* Schedulable task set (U well under 1): each trivial job occupies ~one tick (it
   spins in thread_exit until the next tick preempts it), so demand = sum(1/Pi).
   Coprime periods keep releases spread out; this stays in the normal, non-overloaded
   regime — the regime a scheduler benchmark should characterize. */
static const uint32_t periods[NTASKS] = { 11, 13, 17, 19, 23, 29 };  /* U ~ 0.33 */

/* Trivial job: return immediately (trampolines to thread_exit -> FINISHED), so the
   scheduler re-arms it and next_thread's own cost dominates each dispatch. */
static sys_exit_e job(thread_status_e* status)
{
    (void)status;
    return SYS_OK;
}


/* The ISR updates this count; read it volatile so the wait loops below actually
   observe the updates instead of the compiler hoisting a stale read. */
static inline uint64_t sample_count(void)
{
    return *(volatile uint64_t*)&g_telemetry.metric[SM_SCHED_ALL].h.count;
}


static void reset_metrics(void)
{
    /* Mask ticks so the ISR is not mid-hist_record while we zero the histograms. */
    __asm__ __volatile__("cpsid i" ::: "memory");
    hist_reset(&g_telemetry.metric[0].h);
    hist_reset(&g_telemetry.metric[1].h);
    hist_reset(&g_telemetry.metric[2].h);
    __asm__ __volatile__("cpsie i" ::: "memory");
}


static void schedbench_run(void)
{
    uint32_t ro, po;

    pmu_init();
    telemetry_init(BENCH_ID_SCHEDBENCH);
    telemetry_metric_name(SM_SCHED_ALL, "sched_all");
    telemetry_metric_name(SM_DISPATCH,  "dispatch");
    telemetry_metric_name(SM_IDLE,      "idle");

    pmu_calibrate(&ro, &po);
    g_telemetry.read_overhead_cyc  = ro;   /* used by telem_correct in the hooks */
    g_telemetry.probe_overhead_cyc = po;

    for (uint32_t i = 0; i < NTASKS; i++)
        add_thread(job, periods[i], PERIODIC);

    /* Warmup, then discard those samples and collect the steady-state distribution. */
    while (sample_count() < WARMUP_TICKS) { }
    reset_metrics();
    while (sample_count() < TARGET_SAMPLES) { }

    /* Freeze: stop taking ticks so the metrics don't keep growing (and so we stay well
       below the sustained-load scheduler bug). */
    __asm__ __volatile__("cpsid i" ::: "memory");

    telemetry_done();
    FLAG_WRITE(GENERAL_FLAG, 0x5C);        /* sentinel: bench complete */
    for (;;) { }
}


const workload_t g_workload = { "schedbench", BENCH_ID_SCHEDBENCH, schedbench_run };
