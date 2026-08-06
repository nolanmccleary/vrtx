#include "bsp.h"
#include "preempt_sched.h"
#include "workload.h"

/*
 * System bringup + dispatch. The reset path (startup.s -> c_startup) brings up
 * hardware + heap; main() then performs the standard system initialization that any
 * workload runs on top of, and hands control to the one workload linked into this
 * image. BSP lives in bsp/, the kernel in kernel/, and each benchmark/demo is a
 * pure test payload in bench/ (no system init of its own).
 */
void main(void)
{
    psched_init();
    bsp_gic_init();
    bsp_timer_start();

    g_workload.run();
    for (;;) { }
}
