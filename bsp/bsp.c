#include <stdint.h>
#include "bsp.h"
#include "boot.h"
#include "sequencer.h"
#include "tlsf.h"
#include "preempt_sched.h"
#include "ktrace.h"

#if ENABLE_DCACHE && !ENABLE_MMU
#error ENABLE_DCACHE requires ENABLE_MMU
#endif


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



/////////////////////////////// VECTOR HANDLERS ////////////////////////////////////////////////////

/* Synchronous faults (undef/swi/prefetch/data abort) are handled entirely in
   bsp/startup.s -> bench/fault.c now (fault_capture + fault_halt); no C handler. */


void c_irq_handler(int id)
{
    FLAG_WRITE(VECTOR_FLAG, 0x18);
    switch(id)
    {
        case 0x1b:
#if !DISABLE_WDT
            WDT_L4 = 0x76; //Feed WDT
#endif
            GTIMER_ISR = 1;
            FLAG_WRITE(TICK_MIRROR, TICK_MIRROR + 1);
            next_thread(); 
            break;

        default:
            break;
    }
}


void c_fiq_handler(int id)
{
    FLAG_WRITE(VECTOR_FLAG, 0x1C);
    (void)id;
}


///////////////////////////////////////////// MMU INIT /////////////////////////////

/*
 * ARMv7-A short-descriptor translation tables, identity-mapped (physical == virtual).
 * The tables live in otherwise-unused SDRAM below the heap.
 */
#define L1_TABLE_ADDR              0x00100000u   /* first-level table: 4096 x 1 MB entries (16 KB) */
#define L2_TABLE_ADDR             0x00104000u   /* one second-level table: 256 x 4 KB entries (1 KB) */

#define SECTION_ADDR_SHIFT        20u           /* 1 MB sections */
#define PAGE_ADDR_SHIFT           12u           /* 4 KB pages    */

#define TOTAL_SECTIONS            4096u          /* 4 GB / 1 MB */
#define CACHEABLE_SDRAM_SECTIONS  1024u          /* 0x00000000..0x3FFFFFFF is the DDR heap window */
#define PAGES_PER_SECTION         256u           /* 1 MB / 4 KB */

/* Section 0xFFF (0xFFF00000..0xFFFFFFFF) packs the GIC, the global timer and the
 * 64 KB OCRAM together, so it needs 4 KB granularity to cache OCRAM code without
 * caching the MMIO next to it. */
#define SHARED_MMIO_SECTION       0xFFFu

/* Memory-type attribute bits, OR'd onto the identity base address. */
#define SECTION_ATTR_CACHEABLE    0x15DE6u       /* Normal, write-back write-allocate, shareable */
#define SECTION_ATTR_DEVICE       0x0DE2u        /* Strongly-ordered: MMIO + JTAG-visible RAM (uncached) */
#define PAGE_ATTR_CACHEABLE       0x00576u       /* Normal WBWA shareable, small-page format */
#define PAGE_ATTR_DEVICE          0x00032u       /* Strongly-ordered, small-page format */

/* An L1 entry that delegates its megabyte to a second-level table. */
#define L1_ATTR_POINTS_TO_L2      0x1u           /* descriptor type bits [1:0] = 0b01 */
#define L1_DOMAIN_CLIENT          (15u << 5)     /* domain 15; DACR marks every domain "client" */

/* System / auxiliary control-register bits, applied together once tables are built. */
#define SCTLR_ENABLE_MMU          0x0001u
#define SCTLR_ENABLE_DCACHE       0x0004u
#define SCTLR_ENABLE_ICACHE       0x1000u
#define ACTLR_ENABLE_SMP          0x0040u        /* coherency; must be set before caches on the A9 */
#define DACR_ALL_DOMAINS_CLIENT   0x55555555u


static void mmu_cache_init(void)
{
#if ENABLE_MMU
    volatile uint32_t *l1_table = (volatile uint32_t *)L1_TABLE_ADDR;

    /* Map the DDR heap window cacheable and everything else (peripherals + OCRAM)
       as strongly-ordered device memory. */
    for (uint32_t section = 0; section < TOTAL_SECTIONS; section++)
    {
        uint32_t section_base = section << SECTION_ADDR_SHIFT;
        uint32_t attributes   = (section < CACHEABLE_SDRAM_SECTIONS)
                              ? SECTION_ATTR_CACHEABLE
                              : SECTION_ATTR_DEVICE;

        l1_table[section] = section_base | attributes;
    }

#if ENABLE_ICACHE
    /* Split the shared MMIO/OCRAM megabyte into 4 KB pages: mark only the OCRAM
       code range [_itext_start, _itext_end) cacheable so the I-cache can hold it;
       the GIC, the timer, and the JTAG-visible data pages stay device. */
    extern uint32_t _itext_start;
    extern uint32_t _itext_end;
    uint32_t code_start = (uint32_t)&_itext_start;
    uint32_t code_end   = (uint32_t)&_itext_end;

    volatile uint32_t *l2_table = (volatile uint32_t *)L2_TABLE_ADDR;

    for (uint32_t page = 0; page < PAGES_PER_SECTION; page++)
    {
        uint32_t page_base    = (SHARED_MMIO_SECTION << SECTION_ADDR_SHIFT)
                              + (page << PAGE_ADDR_SHIFT);
        int      is_code_page = (page_base >= code_start) && (page_base < code_end);
        uint32_t attributes   = is_code_page ? PAGE_ATTR_CACHEABLE : PAGE_ATTR_DEVICE;

        l2_table[page] = page_base | attributes;
    }

    l1_table[SHARED_MMIO_SECTION] = L2_TABLE_ADDR | L1_DOMAIN_CLIENT | L1_ATTR_POINTS_TO_L2;
#endif

    uint32_t ttbr0          = L1_TABLE_ADDR;
    uint32_t dacr           = DACR_ALL_DOMAINS_CLIENT;
    uint32_t smp_bit        = ACTLR_ENABLE_SMP;
    uint32_t sctlr_enables  = SCTLR_ENABLE_MMU;
#if ENABLE_DCACHE
    sctlr_enables |= SCTLR_ENABLE_DCACHE;
#endif
#if ENABLE_ICACHE
    sctlr_enables |= SCTLR_ENABLE_ICACHE;
#endif

    uint32_t scratch;
    __asm__ volatile (
        "mrc     p15, 0, %0, c1, c0, 1\n\t"   /* read  ACTLR                         */
        "orr     %0, %0, %4\n\t"              /* set   SMP (coherency, before caches)*/
        "mcr     p15, 0, %0, c1, c0, 1\n\t"   /* write ACTLR                         */
        "isb\n\t"
        "mov     %0, #0\n\t"
        "mcr     p15, 0, %0, c2, c0, 2\n\t"   /* TTBCR = 0  (all translation via TTBR0) */
        "mcr     p15, 0, %1, c2, c0, 0\n\t"   /* TTBR0 = L1 table base               */
        "mcr     p15, 0, %2, c3, c0, 0\n\t"   /* DACR                                */
        "dsb\n\t"
        "mrc     p15, 0, %0, c1, c0, 0\n\t"   /* read  SCTLR                         */
        "orr     %0, %0, %3\n\t"              /* set   MMU / cache enable bits       */
        "mcr     p15, 0, %0, c1, c0, 0\n\t"   /* write SCTLR  (MMU + caches on)      */
        "isb\n"
        : "=&r"(scratch)
        : "r"(ttbr0), "r"(dacr), "r"(sctlr_enables), "r"(smp_bit)
        : "memory"
    );
#elif ENABLE_ICACHE
    uint32_t icache_bit = SCTLR_ENABLE_ICACHE;
    uint32_t scratch;
    __asm__ volatile (
        "mrc     p15, 0, %0, c1, c0, 0\n\t"   /* read  SCTLR                */
        "orr     %0, %0, %1\n\t"              /* set   I-cache (no MMU needed) */
        "mcr     p15, 0, %0, c1, c0, 0\n\t"   /* write SCTLR                */
        "isb\n"
        : "=&r"(scratch)
        : "r"(icache_bit)
        : "memory"
    );
#endif
}


///////////////////////////////////////////// STARTUP /////////////////////////////


void bsp_timer_start(void)
{
    GTIMER_CTRL    = 0;
    GTIMER_ISR     = 1;
    GTIMER_AUTOINC = 199999;
    GTIMER_CMPL    = GTIMER_CNTRL + 199999;
    GTIMER_CMPH    = GTIMER_CNTRH;
    GTIMER_CTRL    = (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0);
}


void bsp_gic_init(void)
{
    GICD_CTLR       = 1;
    GICD_ISENABLER0 |= (1 << 27);
    GICC_PMR        = 0xFF;
    GICC_CTLR       = 1;
}



static void bsp_early_init(void)
{
#if DISABLE_WDT
    /* Hold the L4 watchdogs in reset so no hang/fault ever resets the HPS (which
       drops the JTAG-DP and wedges the USB-Blaster). Dev-only; see Makefile. */
    RSTMGR_PERMODRST |= RSTMGR_PERMODRST_L4WD0 | RSTMGR_PERMODRST_L4WD1;
#endif

#ifdef BOARD_DE1_SOC
    pll_init();
    scan_mgr_init();
    sdram_ctrl_init();
    uint32_t cal = (uint32_t)sdram_calibration_full((struct socfpga_sdr *)0xFFC20000U);
    FLAG_WRITE(SDRAM_TEST_RESULT, cal);
    PL310_FILTER_END   = 0x40000000U;  /* SDRAM window: 0x0..0x3FFFFFFF -> M1 */
    PL310_FILTER_START = 0x00000001U;  /* enable filter, start = 0x0 */
    NIC301_REMAP       = 0;           // Clear remap because system can see sdram exists now 
#endif
    mmu_cache_init();
    FLAG_WRITE(GENERAL_FLAG, 0xBB01);
}



void c_startup(void)
{
    bsp_early_init();
    bsp_gic_init();
    bsp_timer_start();
    heap_init();        // must precede psched_init(): it kMalloc's main_thread + the deque
    psched_init();
}
