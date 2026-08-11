#include <stdint.h>
#include "pmu.h"
#include "telemetry.h"
#include "ktrace.h"          /* SM_SCHED_ALL / SM_DISPATCH / SM_IDLE */
#include "preempt_sched.h"
#include "tlsf.h"
#include "thread.h"
#include "flags.h"
#include "qmeta.h"

/*
 * EDF verification + scheduler-cost benchmark. A fixed periodic task set runs at a
 * target utilization U (-DU_PERMILLE); schedulability (deadline misses, completions)
 * lands in g_edf_result. Over the SAME run, the kernel's KTRACE hooks accumulate
 * per-tick scheduler cost (sched_all/dispatch/idle) into g_telemetry, so sweeping U
 * shows per-tick cost climbing with load -- the mechanism behind U* < 1.
 *
 * Each task's execution Ci (ticks) is a calibrated busy-loop. Calibration runs the
 * loop on the SAME memory the tasks use (an SDRAM heap word): with the MMU off,
 * OCRAM and SDRAM have different (uncached) latency, so calibrating on main_thread's
 * OCRAM stack would undercount the cost and inflate the real utilization.
 */

#ifndef U_PERMILLE
#define U_PERMILLE 900
#endif

#define BENCH_ID_EDF 2
#define NTASKS       3
#define RUN_TICKS    4000        /* ~6.6 hyperperiods of LCM(40,60,100)=600 */
#define CALIB_ITERS  200000u
#define TRACE_TICKS  1200        /* per-tick schedule trace window = 2 hyperperiods */

static const uint32_t T[NTASKS] = { 40, 60, 100 };   /* periods (ticks) */

extern uint32_t gTicks;
extern uint32_t gMissedDeadlines;

static uint32_t g_iters[NTASKS];   /* busy-loop iterations = Ci ticks of CPU */
static uint32_t g_done[NTASKS];    /* completed jobs per task */

static uint32_t start_tick;        /* run window, sampled once tasks are armed */
static uint32_t start_misses;
static uint32_t captured;          /* 1 once the result snapshot is taken */
static uint32_t g_running;         /* 1 once the run window is live (gates trace + capture) */

/* Per-tick schedule trace for the host Gantt: id of the task running each tick. */
uint8_t  g_sched_trace[TRACE_TICKS];   /* 0..2 = task index, 3 = idle */
uint32_t g_trace_len;                  /* valid entries once captured */

typedef struct {
    uint32_t magic;                /* 0x45444631 = "EDF1" */
    uint32_t state;                /* 0 running, 1 done */
    uint32_t u_permille;
    uint32_t ntasks;
    uint32_t run_ticks;
    uint32_t misses;
    uint32_t cyc_per_tick;
    uint32_t C[NTASKS];
    uint32_t Tp[NTASKS];
    uint32_t done[NTASKS];
    uint32_t expected[NTASKS];
} edf_result_t;

edf_result_t g_edf_result;


/* qmeta: describe this workload's result regions + the edf_result field layout so the
   host locates them and decodes them straight from the .elf (no hardcoded offsets). */
const qmeta_region_t g_qmeta_edf_regions[] QMETA_REGIONS = {
    QMETA_REGION("g_edf_result",  &g_edf_result,  sizeof(g_edf_result),  QMETA_KIND_RESULT | QMETA_F_COHERENT),
    QMETA_REGION("g_sched_trace", &g_sched_trace, sizeof(g_sched_trace), QMETA_KIND_GLOBAL | QMETA_F_COHERENT),
    QMETA_REGION("g_trace_len",   &g_trace_len,   sizeof(g_trace_len),   QMETA_KIND_GLOBAL | QMETA_F_COHERENT),
};

const qmeta_field_t g_qmeta_edf_fields[] QMETA_FIELDS = {
    QMETA_FIELD("edf_result", edf_result_t, magic),
    QMETA_FIELD("edf_result", edf_result_t, state),
    QMETA_FIELD("edf_result", edf_result_t, u_permille),
    QMETA_FIELD("edf_result", edf_result_t, ntasks),
    QMETA_FIELD("edf_result", edf_result_t, run_ticks),
    QMETA_FIELD("edf_result", edf_result_t, misses),
    QMETA_FIELD("edf_result", edf_result_t, cyc_per_tick),
    QMETA_FIELD("edf_result", edf_result_t, C),
    QMETA_FIELD("edf_result", edf_result_t, Tp),
    QMETA_FIELD("edf_result", edf_result_t, done),
    QMETA_FIELD("edf_result", edf_result_t, expected),
};


static inline uint32_t rd_ticks(void) { return *(volatile uint32_t*)&gTicks; }

/* Busy-loop over a caller-supplied volatile counter, so its backing memory is the
   caller's choice: a task stack (SDRAM) for jobs, an SDRAM heap word for calibration. */
static void do_work(volatile uint32_t* ctr, uint32_t iters)
{
    for (*ctr = 0; *ctr < iters; (*ctr)++) { }
}

/* One job per task; each spins Ci ticks of CPU, then completes (-> re-armed). */
static sys_exit_e job0(thread_status_e* s) { (void)s; volatile uint32_t c; do_work(&c, g_iters[0]); g_done[0]++; return SYS_OK; }
static sys_exit_e job1(thread_status_e* s) { (void)s; volatile uint32_t c; do_work(&c, g_iters[1]); g_done[1]++; return SYS_OK; }
static sys_exit_e job2(thread_status_e* s) { (void)s; volatile uint32_t c; do_work(&c, g_iters[2]); g_done[2]++; return SYS_OK; }
static sys_exit_e (*const JOBS[NTASKS])(thread_status_e*) = { job0, job1, job2 };

/* Identify the running thread by its job function; idle (main_thread) -> 3. */
static int trace_idx(thread_t* r)
{
    if (r) {
        if (r->func == job0) return 0;
        if (r->func == job1) return 1;
        if (r->func == job2) return 2;
    }
    return 3;
}


void edf_run(void)
{
    pmu_init();

    /* Scheduler-cost channel: the KTRACE hooks in next_thread fill these each tick. */
    telemetry_init(BENCH_ID_EDF);
    telemetry_metric_name(SM_SCHED_ALL, "sched_all");
    telemetry_metric_name(SM_DISPATCH,  "dispatch");
    telemetry_metric_name(SM_IDLE,      "idle");
    uint32_t ro;
    pmu_calibrate(&ro);
    g_telemetry.read_overhead_cyc = ro;

    /* cycles per tick: average over 16 ticks (interrupts on -> gTicks advances). */
    uint32_t t = rd_ticks();
    while (rd_ticks() == t) { }
    uint32_t c0 = pmu_cycles();
    t = rd_ticks();
    while (rd_ticks() < t + 16) { }
    uint32_t cyc_per_tick = (pmu_cycles() - c0) / 16;

    /* cycles per calibration burst, measured on SDRAM (matches the task stacks). */
    volatile uint32_t* scratch = (volatile uint32_t*)kMalloc(sizeof(uint32_t));
    __asm__ __volatile__("cpsid i" ::: "memory");
    uint32_t k0 = pmu_cycles();
    do_work(scratch, CALIB_ITERS);
    uint32_t cyc_calib = pmu_cycles() - k0;
    __asm__ __volatile__("cpsie i" ::: "memory");
    kFree((void*)scratch);

    /* static (run-independent) result fields, known now. */
    g_edf_result.u_permille   = U_PERMILLE;
    g_edf_result.ntasks       = NTASKS;
    g_edf_result.cyc_per_tick = cyc_per_tick;

    /* Ci = U/NTASKS of each period; iterations = Ci ticks worth of CPU cycles. */
    for (int i = 0; i < NTASKS; i++) {
        uint32_t Ci = (U_PERMILLE * T[i]) / (1000u * NTASKS);
        if (Ci < 1) Ci = 1;
        g_edf_result.C[i]  = Ci;
        g_edf_result.Tp[i] = T[i];
        g_iters[i] = (uint32_t)((uint64_t)Ci * cyc_per_tick * CALIB_ITERS / cyc_calib);
    }

    start_tick   = rd_ticks();
    start_misses = gMissedDeadlines;

    /* Arm tasks with IRQs masked (add_thread mutates the deque + allocator the tick ISR
       drains), and reset the cost histograms so they cover only the run window. */
    __asm__ __volatile__("cpsid i" ::: "memory");
    for (int i = 0; i < NTASKS; i++)
        add_thread(JOBS[i], T[i], PERIODIC);
    hist_reset(&g_telemetry.metric[SM_SCHED_ALL].h);
    hist_reset(&g_telemetry.metric[SM_DISPATCH].h);
    hist_reset(&g_telemetry.metric[SM_IDLE].h);
    g_running = 1;
    __asm__ __volatile__("cpsie i" ::: "memory");

    /* Idle. The run window is closed by edf_tick_hook() in the tick ISR. */
    for (;;) { }
}


/* Injected into the tick path (kernel/edf_hook.h) in MODE_EDF builds only. Runs in ISR
   context; snapshots the run once RUN_TICKS elapse, then lets the system keep running.
   magic is written last so the host never observes a partial result. */
void edf_tick_hook(void* running)
{
    if (!g_running) return;

    uint32_t rel = rd_ticks() - start_tick;
    if (rel < TRACE_TICKS)
        g_sched_trace[rel] = (uint8_t)trace_idx((thread_t*)running);

    if (captured) return;
    if (rel < RUN_TICKS) return;

    g_edf_result.run_ticks = rel;
    g_edf_result.misses    = gMissedDeadlines - start_misses;
    for (int i = 0; i < NTASKS; i++) {
        g_edf_result.done[i]     = g_done[i];
        g_edf_result.expected[i] = rel / T[i];
    }
    g_trace_len = (RUN_TICKS < TRACE_TICKS) ? RUN_TICKS : TRACE_TICKS;
    telemetry_done();
    g_edf_result.state = 1;
    g_edf_result.magic = 0x45444631u;
    captured = 1;

    FLAG_WRITE(GENERAL_FLAG, 0xEDF);
}
