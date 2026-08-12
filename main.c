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

void allocbench_run(void);
void edf_run(void);

void main(void)
{
#if defined(MODE_TEST)
    allocbench_run();
    edf_run();
#endif

    for (;;) { }
}
