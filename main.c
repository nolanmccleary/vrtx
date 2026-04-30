#include <stdint.h>
#include <stdbool.h>
#include "allocator.h"

#define CANARY (*(volatile uint32_t *)0xFFFF8000)
#define ALLOC_CHECK (*(volatile uint32_t *)0xFFFF8030)

#define GICD_CTLR       (*(volatile uint32_t *)0xFFFED000)
#define GICD_ISENABLER0 (*(volatile uint32_t *)0xFFFED100)
#define GICC_CTLR       (*(volatile uint32_t *)0xFFFEC100)
#define GICC_PMR        (*(volatile uint32_t *)0xFFFEC104)

#define GTIMER_CNTRL    (*(volatile uint32_t *)0xFFFEC200)
#define GTIMER_CNTRH    (*(volatile uint32_t *)0xFFFEC204)
#define GTIMER_CTRL     (*(volatile uint32_t *)0xFFFEC208)
#define GTIMER_ISR      (*(volatile uint32_t *)0xFFFEC20C)
#define GTIMER_CMPL     (*(volatile uint32_t *)0xFFFEC210)
#define GTIMER_CMPH     (*(volatile uint32_t *)0xFFFEC214)
#define GTIMER_AUTOINC  (*(volatile uint32_t *)0xFFFEC218)

#define WDT_L4 (*(volatile uint32_t*)0xFFD0200C)



static void gtimer_init(void)
{
    GTIMER_CTRL    = 0;
    GTIMER_ISR     = 1;
    GTIMER_AUTOINC = 399999;                    //400k counts/tick. 1000Hz if timer fires at 400k
    GTIMER_CMPL    = GTIMER_CNTRL + 399999;
    GTIMER_CMPH    = GTIMER_CNTRH;
    GTIMER_CTRL    = (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0);
}


static void gic_init(void)
{
    GICD_CTLR       = 1;
    GICD_ISENABLER0 |= (1 << 27);
    GICC_PMR        = 0xFF;
    GICC_CTLR       = 1;
}


void c_irq_handler(int id)
{
    switch(id)
    {
        case 0x1b: //gtimer interrupt
            WDT_L4 = 0x76; //old yeller his ass
            GTIMER_ISR = 1;
            CANARY++;
            break;

        default:
            break;
    }
}


void c_fiq_handler(int r0)
{
    (void)r0;
}

void main(void)
{
    gic_init();
    gtimer_init();
    CANARY = 0;

    heap_init();

    uint32_t* test1 = (uint32_t*)kMalloc(sizeof(uint32_t));
    uint32_t* test2 = (uint32_t*)kMalloc(sizeof(uint32_t));
    uint32_t* test3 = (uint32_t*)kMalloc(sizeof(uint32_t));

    *test3 = 69;




    while (1) 
    {
        ALLOC_CHECK = *test3;
    }
}
