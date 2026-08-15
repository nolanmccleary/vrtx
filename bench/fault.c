#include "fault.h"
#include "boot.h"   /* RSTMGR_PERMODRST */


/* Lives in the merged host-shared NOLOAD OCRAM region (linker .fault input). */
fault_record_t g_fault __attribute__((section(".fault"), used));


/* L4 watchdog module reset bits in RSTMGR permodrst (Cyclone V): l4wd0=6, l4wd1=7. */
#define RSTMGR_PERMODRST_L4WD0  (1U << 6)
#define RSTMGR_PERMODRST_L4WD1  (1U << 7)


/* CP15 fault status/address registers (ARMv7-A). */
static inline uint32_t rd_dfsr(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c5,c0,0" : "=r"(v)); return v; }
static inline uint32_t rd_dfar(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c6,c0,0" : "=r"(v)); return v; }
static inline uint32_t rd_ifsr(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c5,c0,1" : "=r"(v)); return v; }
static inline uint32_t rd_ifar(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c6,c0,2" : "=r"(v)); return v; }


void fault_capture(uint32_t pc, uint32_t spsr, uint32_t vec)
{
    g_fault.vec  = vec;
    g_fault.pc   = pc;
    g_fault.spsr = spsr;
    g_fault.dfsr = rd_dfsr();
    g_fault.dfar = rd_dfar();
    g_fault.ifsr = rd_ifsr();
    g_fault.ifar = rd_ifar();

    __asm__ volatile("dmb sy" ::: "memory");
    g_fault.magic = FAULT_MAGIC;   /* publish last */
    __asm__ volatile("dmb sy" ::: "memory");
}


/* Empty marker the host installs a hardware breakpoint on (same pattern as the
   ktrace_bp_* markers). Reaching it means a fault was captured. */
__attribute__((noinline, used, aligned(4)))
void fault_trap(void)
{
    __asm__ volatile("nop" ::: "memory");
}


void fault_halt(void)
{
    /* Hold the L4 watchdogs in reset so they stop counting -- the trap/spin below
       then never triggers an HPS reset, keeping g_fault and the live registers
       intact for OpenOCD to read. */
    RSTMGR_PERMODRST |= RSTMGR_PERMODRST_L4WD0 | RSTMGR_PERMODRST_L4WD1;

    fault_trap();

    for (;;) { }
}
