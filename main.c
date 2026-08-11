#include "bsp.h"
#include "preempt_sched.h"
#include "tlsf.h"

/*
 * System bringup + dispatch. startup.s -> c_startup brings up hardware + heap;
 * main() does the standard system init, then runs the one benchmark selected at
 * build time (-DMODE_* from the Makefile). Benchmarks live in bench/ and are pure
 * test payload.
 */

void allocbench_run(void);
void edf_run(void);

void main(void)
{
    bsp_gic_init();
    bsp_timer_start();
    heap_init();        // must precede psched_init(): it kMalloc's main_thread + the deque
    psched_init();

#if defined(MODE_ALLOCBENCH)
    allocbench_run();
#else
    edf_run();
#endif

    for (;;) { }
}
