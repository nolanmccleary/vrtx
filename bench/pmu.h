#ifndef __PMU_H__
#define __PMU_H__

#include <stdint.h>

/*
 * Cortex-A9 Performance Monitoring Unit — cycle-accurate, low-overhead timing.
 *
 * PMCCNTR (CP15 c9,c13,0) counts CPU cycles 1:1 with the divider disabled. It is
 * the primary measurement instrument: cycles are the comparison currency (a value
 * measured here is directly comparable to the same event on any other A9). The
 * 64-bit A9 global timer (PERIPHBASE+0x200) is the wall-clock reference for spans
 * beyond the 32-bit cycle wrap and for pinning the absolute clock rate later.
 */

/* Global timer registers (A9 MPCore private memory region). */
#define GTIMER_CNTR_LO (*(volatile uint32_t *)0xFFFEC200)
#define GTIMER_CNTR_HI (*(volatile uint32_t *)0xFFFEC204)

/* Enable the PMU and start a free-running, full-resolution cycle counter. */
void pmu_init(void);

/*
 * Calibration: minimum back-to-back read cost (subtracted from every sample) and
 * the full empty-probe cost (informational). Best-case (min) is used so we remove
 * the pure read overhead, not a cache-cold outlier.
 */
void pmu_calibrate(uint32_t *read_overhead_cyc, uint32_t *probe_overhead_cyc);

/*
 * Read the cycle counter. The ISB retires preceding instructions before the count
 * is sampled, and the "memory" clobber blocks the compiler from moving it. Bracket
 * a region as: t0 = pmu_cycles(); <region>; delta = pmu_cycles() - t0;
 */
static inline uint32_t pmu_cycles(void)
{
    uint32_t c;
    __asm__ __volatile__("isb\n\t"
                         "mrc p15, 0, %0, c9, c13, 0\n\t" //Sys control coprocessor, null opc1 sel, gcc choose targ reg, PMU reg bank, secondary reg sel is c(oprocessor reg)13
                         : "=r"(c)
                         :
                         : "memory");
    return c;
}

/* 64-bit global timer read with a hi/lo/hi re-read guard against rollover. */
static inline uint64_t pmu_gtimer(void)
{
    uint32_t hi, lo, hi2;
    do {
        hi  = GTIMER_CNTR_HI;
        lo  = GTIMER_CNTR_LO;
        hi2 = GTIMER_CNTR_HI;
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

#endif
