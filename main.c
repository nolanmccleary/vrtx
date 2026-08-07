#include "bsp.h"
#include "preempt_sched.h"

/*
 * System bringup + dispatch. startup.s -> c_startup brings up hardware + heap;
 * main() does the standard system init, then runs the one workload selected at
 * build time (-DMODE_* from the Makefile). Workloads live in bench/ and are pure
 * test payload.
 */

void demo_run(void);
void selftest_run(void);
void schedbench_run(void);
void stress_run(void);

void main(void)
{
    bsp_gic_init();
    bsp_timer_start();
    psched_init();

#if   defined(MODE_SELFTEST)
    selftest_run();
#elif defined(MODE_SCHEDBENCH)
    schedbench_run();
#elif defined(MODE_STRESS)
    stress_run();
#else
    demo_run();
#endif

    for (;;) { }
}
