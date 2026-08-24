#ifndef __TIMERS_H__
#define __TIMERS_H__

#include <stdint.h>


#define GTIMER_CNTRL    (*(volatile uint32_t *)0xFFFEC200)
#define GTIMER_CNTRH    (*(volatile uint32_t *)0xFFFEC204)
#define GTIMER_CTRL     (*(volatile uint32_t *)0xFFFEC208)
#define GTIMER_ISR      (*(volatile uint32_t *)0xFFFEC20C)
#define GTIMER_CMPL     (*(volatile uint32_t *)0xFFFEC210)
#define GTIMER_CMPH     (*(volatile uint32_t *)0xFFFEC214)
#define GTIMER_AUTOINC  (*(volatile uint32_t *)0xFFFEC218)

#define WDT_L4 (*(volatile uint32_t*)0xFFD0200C)


void cpu0_timer_start(void);
void cpu1_timer_start(void);


#endif

