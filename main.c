#include <stdint.h>
#include "bsp.h"
#include "preempt_sched.h"
#include "tlsf.h"
#include "ktrace.h"


#if ENABLE_SMP
volatile uint32_t g_cpu1_ready;
#endif


static void c_startup(void)
{
#if ENABLE_SMP
    while (!g_cpu1_ready) { }    /* wait for CPU1 in its spin loop before the remap below */
#endif

    bsp_sdram_init();               /* PLL/scan-mgr/SDRAM/NIC301 -- prime self-boot suspect */
    bsp_mmu_and_cache_init();    /* re-enables the MMU with our tables -- other suspect */
    bsp_gic_init();
    bsp_timer_start();

    pmu_init();
    heap_init();        // must precede psched_init(): it kMalloc's main_thread + the deque
    psched_init();
}





void allocbench_run(void);
void edf_run(void);

void main(void)
{
    KTRACE_WAIT_BOOT();   /* BOOT_TEST self-boot builds: hang here until JTAG releases (no-op otherwise) */
    c_startup();

#if defined(MODE_TEST)
    allocbench_run();
    edf_run();
#endif

    for (;;) { }
}
