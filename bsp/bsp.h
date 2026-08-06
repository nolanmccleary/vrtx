#ifndef __BSP_H__
#define __BSP_H__

/*
 * Board support: hardware bringup and the workload-facing HW API. The reset path
 * (startup.s) calls c_startup(); the vector handlers (c_*_handler) are defined here
 * and referenced by startup.s by name. A workload brings up exactly the subsystems
 * it needs via the calls below, so an image that never starts the timer boots quiescent.
 */

void bsp_gic_init(void);       /* enable GIC distributor + CPU interface, timer IRQ line */
void bsp_timer_start(void);    /* configure + start the periodic tick (private/global timer) */
void bsp_sdram_selftest(void); /* write/verify SDRAM; reports via the flag scoreboard */

#endif
