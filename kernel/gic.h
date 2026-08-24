#ifndef __GIC_H__
#define __GIC_H__

#include <stdint.h>


#define GICD_BASE       0xFFFED000u

#define GICD_CTLR       (*(volatile uint32_t *)(GICD_BASE + 0x000u))
#define GICD_ISENABLER0 (*(volatile uint32_t *)(GICD_BASE + 0x100u))
#define GICD_SGIR       (*(volatile uint32_t *)(GICD_BASE + 0xF00u))
#define GICC_CTLR       (*(volatile uint32_t *)0xFFFEC100)
#define GICC_PMR        (*(volatile uint32_t *)0xFFFEC104)


void cpu0_gic_init(void);
void cpu1_gic_init(void);


#endif
