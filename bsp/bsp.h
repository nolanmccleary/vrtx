#ifndef __BSP_H__
#define __BSP_H__


void bsp_sdram_init(void);
void bsp_mmu_and_cache_init(void);
void bsp_gic_init(void);       /* enable GIC distributor + CPU interface, timer IRQ line */
void bsp_timer_start(void);    /* configure + start the periodic tick (private/global timer) */

#endif
