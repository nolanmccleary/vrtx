#include <stdint.h>
#include <stdbool.h>
#include "allocator.h"
#include "boot/boot.h"
#include "flags.h"
#include "preempt_sched.h"
#include "boot/sequencer.h"


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
    GTIMER_AUTOINC = 199999;                    //400k counts/tick. 1000Hz if timer fires at 400k
    GTIMER_CMPL    = GTIMER_CNTRL + 199999;
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



void c_reset_handler(void)
{
    VECTOR_FLAG = 0x80;
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
            next_thread();
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



///////////////////////////////////////////// SDRAM TEST ////////////////////////////////
#define SDRAM_BASE       ((volatile uint32_t *)0x00000000)
#define SDRAM_TEST_WORDS 64

static void sdram_test(void)
{
    volatile uint32_t *p = SDRAM_BASE;

    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++)
        p[i] = i;

    __asm__ volatile ("dsb" ::: "memory");
    GENERAL_FLAG = 0xA002;  /* writes done */

    for (uint32_t i = 0; i < SDRAM_TEST_WORDS; i++) {
        if (p[i] != i) {
            SDRAM_TEST_RESULT = (uint32_t)&p[i];
            GENERAL_FLAG = 0x813;
            return;
        }
    }

    GENERAL_FLAG = 0x814;
    SDRAM_TEST_RESULT = 0xDEAD0000;
}


////////////////////////////// Sched test /////////////////////////////////////////




static sys_exit_e pthread1(thread_status_e* status)
{
    (void)status;
    while (1)
        THREAD_COUNT_1++;
    return SYS_OK;
}


static sys_exit_e pthread2(thread_status_e* status)
{
    (void)status;
    while (1)
        THREAD_COUNT_2++;
    return SYS_OK;
}





///////////////////////////////////////////// MAIN LOOP /////////////////////////////
void main(void)
{
    pll_init();
    scan_mgr_init();
    sdram_ctrl_init();
    SDRAM_TEST_RESULT = (uint32_t)sdram_calibration_full((struct socfpga_sdr *)0xFFC20000U);
    PL310_FILTER_END   = 0x40000000U;  /* SDRAM window: 0x0..0x3FFFFFFF -> M1 */
    PL310_FILTER_START = 0x00000001U;  /* enable filter, start = 0x0 */
    NIC301_REMAP       = 0;            /* SDRAM at 0x0 on L3 NIC path too */
    GENERAL_FLAG = 0xBB01;

    heap_init();
    psched_init();
    add_thread(pthread1, HIGH, 1);
    add_thread(pthread2, HIGH, 2);

    gic_init();
    gtimer_init();
    sdram_test();


    uint32_t* test1 = (uint32_t*)kMalloc(sizeof(uint32_t));
    uint32_t* test2 = (uint32_t*)kMalloc(sizeof(uint32_t));
    uint32_t* test3 = (uint32_t*)kMalloc(sizeof(uint32_t));

    kFree(test1);
    kFree(test2);
    kFree(test3);

    *test3 = 0x67;
    VECTOR_FLAG = 0x1F;

    while (1)
    {
        ALLOC_CHECK = *test3;
        GENERAL_FLAG = 0x69;
    }
}
