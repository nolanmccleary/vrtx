#include "bsp.h"
#include "preempt_sched.h"
#include "tlsf.h"
#include "ktrace.h"

/*
 * System bringup + dispatch. startup.s -> c_startup brings up hardware + heap;
 * main() does the standard system init, then runs the one benchmark selected at
 * build time (-DMODE_* from the Makefile). Benchmarks live in bench/ and are pure
 * test payload.
 */



static void c_startup(void)
{
    bsp_board_init();
    bsp_memory_and_cache_init();
    bsp_gic_init();
    bsp_timer_start();
    
    pmu_init();
    pmu_calibrate();
    
    heap_init();        // must precede psched_init(): it kMalloc's main_thread + the deque
    psched_init();
}





void allocbench_run(void);
void edf_run(void);

void main(void)
{
    c_startup();
#if defined(MODE_TEST)
    allocbench_run();
    edf_run();
#endif

    for (;;) { }
}
