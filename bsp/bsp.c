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


// FYI I am calling Sections and Pages here L1 and L2 blocks because I think they were poorly chosen terms and this is my project so I do what I want



extern uint32_t _l1_table_base[];   /* reserved 16384-byte L1 table in DDR (16 KB-aligned) */
extern uint32_t _l2_table_base[];   /* reserved 1024-byte  L2 table in DDR (1 KB-aligned)  */
extern char     _sdram_length[];    /* = LENGTH(SDRAM): total DDR size in bytes */
extern char     _ocram_origin[];    /* = ORIGIN(OCRAM): OCRAM base address      */



#define L1_BLOCK_ADDR_SHIFT       20u                                  /* block base addr = block_index << 20 */
#define L1_BLOCK_SIZE_BYTES       (1u << L1_BLOCK_ADDR_SHIFT)          /* 1048576 bytes per L1 block */
#define L1_BLOCK_COUNT            (1u << (32u - L1_BLOCK_ADDR_SHIFT))  /* 4096 blocks cover the whole 4.29e9-byte space */


#define L2_BLOCK_ADDR_SHIFT       12u                                        /* sub-block base = index << 12 */
#define L2_BLOCK_SIZE_BYTES       (1u << L2_BLOCK_ADDR_SHIFT)                /* 4096 bytes per L2 sub-block */
#define L2_BLOCK_COUNT            (L1_BLOCK_SIZE_BYTES / L2_BLOCK_SIZE_BYTES)/* 1048576 / 4096 = 256 sub-blocks per L1 block */



#define L1_ENTRY_MAP_BLOCK_CACHEABLE  0x15DE6u  /* kind 0b10: maps its 1048576-byte block directly; Normal WBWA shareable */
#define L1_ENTRY_MAP_BLOCK_DEVICE     0x0DE2u   /* kind 0b10: maps its block directly; Strongly-ordered (uncached) */
#define L1_ENTRY_POINT_TO_L2          0x001E1u  /* kind 0b01: no memory type; delegates the block to an L2 table (domain 15) */

/* Complete L2 sub-block entries except the address field -- OR the sub-block base on. */
#define L2_ENTRY_MAP_SUBBLOCK_CACHEABLE  0x00576u  /* Normal WBWA shareable, small sub-block layout */
#define L2_ENTRY_MAP_SUBBLOCK_DEVICE     0x00032u  /* Strongly-ordered,       small sub-block layout */

#define SCTLR_ENABLE_MMU          0x0001u
#define SCTLR_ENABLE_DCACHE       0x0004u
#define SCTLR_ENABLE_ICACHE       0x1000u
#define ACTLR_ENABLE_SMP          0x0040u        /* coherency; must be set before caches on the A9 */
#define DACR_ALL_DOMAINS_CLIENT   0x55555555u


static void mmu_cache_init(void)
{
#if ENABLE_MMU
    volatile uint32_t *l1_table = _l1_table_base;

    uint32_t sdram_block_count = (uint32_t)(uintptr_t)_sdram_length / L1_BLOCK_SIZE_BYTES;

#if ENABLE_ICACHE
    uint32_t delegated_block = (uint32_t)(uintptr_t)_ocram_origin >> L1_BLOCK_ADDR_SHIFT;
#endif

    /* Map each block directly: SDRAM blocks cacheable, every other block device. */
    for (uint32_t l1_block = 0; l1_block < L1_BLOCK_COUNT; l1_block++)
    {

#if ENABLE_ICACHE
        if (l1_block == delegated_block)
            continue;                 /* handled by the L2 delegation below, not here */
#endif

        uint32_t l1_block_base = l1_block << L1_BLOCK_ADDR_SHIFT;
        uint32_t l1_entry      = (l1_block < sdram_block_count) ? L1_ENTRY_MAP_BLOCK_CACHEABLE : L1_ENTRY_MAP_BLOCK_DEVICE;
        l1_table[l1_block] = l1_block_base | l1_entry;
    }

#if ENABLE_ICACHE
    /* Fill the L2 table for the delegated block: the OCRAM code range
       [_itext_start, _itext_end) is cacheable (so the I-cache can hold it); the GIC, the
       timer, and the JTAG-visible data sub-blocks are device. This L2 table is the SOLE
       source of memory type for that block. */
    extern uint32_t _itext_start;
    extern uint32_t _itext_end;
    uint32_t code_start = (uint32_t)&_itext_start;
    uint32_t code_end   = (uint32_t)&_itext_end;

    volatile uint32_t *l2_table = _l2_table_base;

    for (uint32_t l2_block = 0; l2_block < L2_BLOCK_COUNT; l2_block++)
    {
        uint32_t l2_block_base = (delegated_block << L1_BLOCK_ADDR_SHIFT)
                               + (l2_block << L2_BLOCK_ADDR_SHIFT);
        int      is_code_block = (l2_block_base >= code_start) && (l2_block_base < code_end);
        uint32_t l2_entry      = is_code_block ? L2_ENTRY_MAP_SUBBLOCK_CACHEABLE
                                               : L2_ENTRY_MAP_SUBBLOCK_DEVICE;

        l2_table[l2_block] = l2_block_base | l2_entry;
    }

    l1_table[delegated_block] = (uint32_t)(uintptr_t)_l2_table_base | L1_ENTRY_POINT_TO_L2;
#endif

    uint32_t ttbr0          = (uint32_t)(uintptr_t)_l1_table_base;
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



static void bsp_memory_and_cache_init(void)
{
    /* CPU1 is held in reset as the very first thing in _reset_handler (bsp/startup.s),
       before any of this runs -- see the RSTMGR_MPUMODRST write there. */

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
    bsp_memory_and_cache_init();
    bsp_gic_init();
    bsp_timer_start();
    heap_init();        // must precede psched_init(): it kMalloc's main_thread + the deque
    psched_init();
}
