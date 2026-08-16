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

static void mmu_cache_init(void)
{
#if ENABLE_MMU
    volatile uint32_t *ttb = (volatile uint32_t *)0x00100000u;
    uint32_t r;

    /* SDRAM (0x0..0x40000000, sections 0..1023) = Normal, WB write-allocate,
       shareable (cacheable). Everything above (peripherals + OCRAM) = device /
       strongly-ordered. Caching the SDRAM heap is what makes the D-cache do work;
       OCRAM stays uncached so JTAG reads of the host-shared region stay coherent. */
    for (int i = 4095; i >= 0; i--)
        ttb[i] = ((uint32_t)i << 20) | (i < 1024 ? 0x15DE6u : 0x0DE2u);

#if ENABLE_ICACHE                                   //Keep GIC and other MMIO + JTAG read targets uncached, requires 2 level translation for more page granularity
    extern uint32_t _itext_start, _itext_end;
    uint32_t itext_lo = (uint32_t)&_itext_start;
    uint32_t itext_hi = (uint32_t)&_itext_end;

    volatile uint32_t *l2 = (volatile uint32_t *)0x00104000u;   /* just past the 16 KB L1 table */
    for (int p = 0; p < 256; p++)
    {
        uint32_t va = 0xFFF00000u + ((uint32_t)p << 12);
        int cache = (va >= itext_lo && va < itext_hi);
        l2[p] = va | (cache ? 0x576u : 0x032u);
    }

    /* L1[0xFFF] -> page-table descriptor (bits[1:0]=01) at the L2 base, domain 15. */
    ttb[0xFFF] = 0x00104000u | (15u << 5) | 0x1u;
#endif

    uint32_t ttbr = 0x00100000u;
    uint32_t dacr = 0x55555555u;
    uint32_t sctlr_bits = 0x1u;  /* M = MMU */
#if ENABLE_DCACHE
    sctlr_bits |= 0x4u;          /* C = D-cache */
#endif
#if ENABLE_ICACHE
    sctlr_bits |= 0x1000u;       /* I = I-cache */
#endif
    __asm__ volatile (
        /* ACTLR.SMP = 1 (bit 6). REQUIRED on the A9 before enabling caches: with
           SMP=0 the core treats Normal *Shareable* memory as Non-cacheable, so the
           D-cache never allocates for our shareable SDRAM heap (descriptor S bit).
           Must be set before SCTLR.C. */
        "mrc     p15, 0, %0, c1, c0, 1\n\t"
        "orr     %0, %0, #0x40\n\t"
        "mcr     p15, 0, %0, c1, c0, 1\n\t"
        "isb\n\t"
        "mov     %0, #0\n\t"
        "mcr     p15, 0, %0, c2, c0, 2\n\t"   /* TTBCR = 0 */
        "mcr     p15, 0, %1, c2, c0, 0\n\t"   /* TTBR0 */
        "mcr     p15, 0, %2, c3, c0, 0\n\t"   /* DACR = all client */
        "dsb\n\t"
        "mrc     p15, 0, %0, c1, c0, 0\n\t"
        "orr     %0, %0, %3\n\t"
        "mcr     p15, 0, %0, c1, c0, 0\n\t"
        "isb\n"
        : "=&r"(r)
        : "r"(ttbr), "r"(dacr), "r"(sctlr_bits)
        : "memory"
    );
#elif ENABLE_ICACHE
    uint32_t r;
    __asm__ volatile (
        "mrc     p15, 0, %0, c1, c0, 0\n\t"
        "orr     %0, %0, #0x1000\n\t"          /* I = I-cache (no MMU needed) */
        "mcr     p15, 0, %0, c1, c0, 0\n\t"
        "isb\n"
        : "=&r"(r) : : "memory"
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
