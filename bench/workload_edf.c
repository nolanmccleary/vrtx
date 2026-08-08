#include <stdint.h>
#include "pmu.h"
#include "preempt_sched.h"
#include "thread.h"
#include "flags.h"

/*
 * EDF verification. Runs a fixed periodic task set at a target utilization U
 * (-DU_PERMILLE) and counts deadline misses, to find the empirical schedulable
 * bound U*. Each task's execution Ci (in ticks) is realized as a calibrated
 * busy-loop, so U = sum(Ci/Ti) is controlled. Sweep U across builds to map the
 * knee; U* < 1 by the scheduler overhead + tick quantization + the no-yield
 * thread_exit tail. Results go in g_edf_result for the host to read.
 */

#ifndef U_PERMILLE
#define U_PERMILLE 900          /* target utilization x1000 */
#endif

#define NTASKS      3
#define RUN_TICKS   4000        /* ~6.6 hyperperiods of LCM(40,60,100)=600 */
#define CALIB_ITERS 200000u

static const uint32_t T[NTASKS] = { 40, 60, 100 };   /* periods (ticks) */

extern uint32_t gTicks;
extern uint32_t gMissedDeadlines;

static uint32_t g_iters[NTASKS];   /* busy-loop iterations = Ci ticks of CPU */
static uint32_t g_done[NTASKS];    /* completed jobs per task */

static uint32_t start_tick;        /* run window, sampled once tasks are armed */
static uint32_t start_misses;
static uint32_t captured;          /* 1 once the result snapshot is taken */

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


static void do_work(uint32_t iters)
{
    for (volatile uint32_t i = 0; i < iters; i++) { }
}

/* One job function per task so each knows its Ci and completion counter. */
static sys_exit_e job0(thread_status_e* s) { (void)s; do_work(g_iters[0]); g_done[0]++; return SYS_OK; }
static sys_exit_e job1(thread_status_e* s) { (void)s; do_work(g_iters[1]); g_done[1]++; return SYS_OK; }
static sys_exit_e job2(thread_status_e* s) { (void)s; do_work(g_iters[2]); g_done[2]++; return SYS_OK; }
static sys_exit_e (*const JOBS[NTASKS])(thread_status_e*) = { job0, job1, job2 };

static inline uint32_t rd_ticks(void) { return *(volatile uint32_t*)&gTicks; }


void edf_run(void)
{
    pmu_init();

    /* cycles per tick: average over 16 ticks (interrupts on -> gTicks advances). */
    uint32_t t = rd_ticks();
    while (rd_ticks() == t) { }
    uint32_t c0 = pmu_cycles();
    t = rd_ticks();
    while (rd_ticks() < t + 16) { }
    uint32_t cyc_per_tick = (pmu_cycles() - c0) / 16;

    /* cycles per calibration burst (interrupts off -> pure loop). */
    __asm__ __volatile__("cpsid i" ::: "memory");
    uint32_t k0 = pmu_cycles();
    do_work(CALIB_ITERS);
    uint32_t cyc_calib = pmu_cycles() - k0;
    __asm__ __volatile__("cpsie i" ::: "memory");

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

    /* Mask IRQs while arming the tasks: add_thread mutates the incoming deque and the
       allocator free-list, which the tick ISR drains concurrently (pop_front -> kFree).
       Temporary guard until the deque/allocator get real locks. */
    __asm__ __volatile__("cpsid i" ::: "memory");
    for (int i = 0; i < NTASKS; i++)
        add_thread(JOBS[i], T[i], PERIODIC);
    __asm__ __volatile__("cpsie i" ::: "memory");

    /* Idle. The run window is closed by edf_tick_hook() in the tick ISR, which snapshots
       the result once RUN_TICKS have elapsed (main_thread would starve under load). */
    for (;;) { }
}


/* Injected into the tick path (kernel/edf_hook.h) in MODE_EDF builds only. Runs in ISR
   context; snapshots the run once, then lets the system keep running. magic is written
   last so the host never observes a partial result. */
void edf_tick_hook(void)
{
    if (captured) return;
    if (rd_ticks() - start_tick < RUN_TICKS) return;

    g_edf_result.run_ticks = rd_ticks() - start_tick;
    g_edf_result.misses    = gMissedDeadlines - start_misses;
    for (int i = 0; i < NTASKS; i++) {
        g_edf_result.done[i]     = g_done[i];
        g_edf_result.expected[i] = g_edf_result.run_ticks / T[i];
    }
    g_edf_result.state = 1;
    g_edf_result.magic = 0x45444631u;   /* last: signals "complete" to the host */
    captured = 1;

    FLAG_WRITE(GENERAL_FLAG, 0xEDF);
}
