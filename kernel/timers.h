#ifndef __TIMERS_H__
#define __TIMERS_H__

#include <stdint.h>
#include "board.h"


#define GTIMER_CNTRL    (*(volatile uint32_t *)(BOARD_GTIMER_BASE + 0x00u))
#define GTIMER_CNTRH    (*(volatile uint32_t *)(BOARD_GTIMER_BASE + 0x04u))
#define GTIMER_CTRL     (*(volatile uint32_t *)(BOARD_GTIMER_BASE + 0x08u))
#define GTIMER_ISR      (*(volatile uint32_t *)(BOARD_GTIMER_BASE + 0x0Cu))
#define GTIMER_CMPL     (*(volatile uint32_t *)(BOARD_GTIMER_BASE + 0x10u))
#define GTIMER_CMPH     (*(volatile uint32_t *)(BOARD_GTIMER_BASE + 0x14u))
#define GTIMER_AUTOINC  (*(volatile uint32_t *)(BOARD_GTIMER_BASE + 0x18u))

#define WDT_L4 (*(volatile uint32_t*)BOARD_WDT_BASE)


void cpu0_timer_start(void);
void cpu1_timer_start(void);


#endif

