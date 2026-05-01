#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include "allocator.h"


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


//////////////////////////////////// STATUS REGS ////////////////////////////////////////////////////

extern char _status_base;

#define VECTOR_FLAG      (*(volatile uint32_t*)&_status_base)
#define TICK_MIRROR      (*((volatile uint32_t*)&_status_base + 1))
#define ALLOC_CHECK      (*((volatile uint32_t*)&_status_base + 2))
#define SDRAM_TEST_RESULT (*((volatile uint32_t*)&_status_base + 3))


////////////////////////////////////// HW INIT //////////////////////////////////////////////////////////

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


/////////////////////////////// VECTOR HANDLERS ////////////////////////////////////////////////////
uint32_t gTick = 0;



void c_reset_handler(void)
{
    VECTOR_FLAG = 0x00;
}


void c_undef_handler(void)
{
    VECTOR_FLAG = 0x04;
}


void c_swi_handler(void)
{
    VECTOR_FLAG = 0x08;
}


void c_prefetch_handler(void)
{
    VECTOR_FLAG = 0x0C;
}


void c_data_handler(void)
{
    VECTOR_FLAG = 0x10;
}


void c_irq_handler(int id)
{
    VECTOR_FLAG = 0x18;
    switch(id)
    {
        case 0x1b: //gtimer interrupt
            WDT_L4 = 0x76; //old yeller his ass
            GTIMER_ISR = 1;
            TICK_MIRROR++;
            gTick++;
            break;

        default:
            break;
    }
}


void c_fiq_handler(int id)
{
    VECTOR_FLAG = 0x1C;
    (void)id;
}



///////////////////////////////////////////// SDRAM TEST ////////////////////////////////////////////////////

#define SDRAM_BASE       ((volatile uint32_t *)0x00000000)
#define SDRAM_TEST_WORDS (256 * 1024)  /* 1MB */

static void sdram_test(void)
{
    volatile uint32_t *p = SDRAM_BASE;

    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++)
        p[i] = i;

    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++) {
        if (p[i] != i) {
            SDRAM_TEST_RESULT = (uint32_t)&p[i];  /* first failing address */
            return;
        }
    }

    SDRAM_TEST_RESULT = 0xDEAD0000;  /* pass sentinel */
}


///////////////////////////////////////////// MAIN LOOP ////////////////////////////////////////////////////

void main(void)
{
    gic_init();
    gtimer_init();
    // sdram_test();

    // heap_init();

    // uint32_t* test1 = (uint32_t*)kMalloc(sizeof(uint32_t));
    // uint32_t* test2 = (uint32_t*)kMalloc(sizeof(uint32_t));
    // uint32_t* test3 = (uint32_t*)kMalloc(sizeof(uint32_t));

    // *test3 = 69;

    while (1) 
    {
        // ALLOC_CHECK = *test3;
        VECTOR_FLAG = 0x1F;
    }
}
