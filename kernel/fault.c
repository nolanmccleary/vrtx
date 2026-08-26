#include "fault.h"
#include "cpu.h"
#include "boot.h" 


fault_record_t g_fault[NUM_CPUS] __attribute__((section(".fault"), used));


static inline uint32_t rd_dfsr(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c5,c0,0" : "=r"(v)); return v; }
static inline uint32_t rd_dfar(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c6,c0,0" : "=r"(v)); return v; }
static inline uint32_t rd_ifsr(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c5,c0,1" : "=r"(v)); return v; }
static inline uint32_t rd_ifar(void) { uint32_t v; __asm__ volatile("mrc p15,0,%0,c6,c0,2" : "=r"(v)); return v; }


void fault_capture(uint32_t pc, uint32_t spsr, uint32_t vec)
{
    cpu_core_e core = curr_core();

    g_fault[core].vec  = vec;
    g_fault[core].pc   = pc;
    g_fault[core].spsr = spsr;
    g_fault[core].dfsr = rd_dfsr();
    g_fault[core].dfar = rd_dfar();
    g_fault[core].ifsr = rd_ifsr();
    g_fault[core].ifar = rd_ifar();

    __asm__ volatile("dmb sy" ::: "memory");
    g_fault[core].magic = FAULT_MAGIC;
    __asm__ volatile("dmb sy" ::: "memory");
}


__attribute__((noinline, used, aligned(4)))
void fault_trap(void)
{
    __asm__ volatile("nop" ::: "memory");
}


void fault_halt(void)
{
    RSTMGR_PERMODRST |= RSTMGR_PERMODRST_L4WD0 | RSTMGR_PERMODRST_L4WD1;

    fault_trap();

    for (;;) { }
}
