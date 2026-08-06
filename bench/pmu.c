#include "pmu.h"

/* PMCR bits (CP15 c9,c12,0). */
#define PMCR_E (1u << 0) /* enable all counters      */
#define PMCR_C (1u << 2) /* reset cycle counter      */
#define PMCR_D (1u << 3) /* cycle count divider (/64)*/

/* PMCNTENSET bit 31 = cycle counter (CP15 c9,c12,1). */
#define PMCNTEN_CCNT (1u << 31)


void pmu_init(void)
{
    uint32_t v;

    /* PMCR: enable + reset the cycle counter, divider off (count every cycle). */
    __asm__ __volatile__("mrc p15, 0, %0, c9, c12, 0" : "=r"(v));
    v |=  (PMCR_E | PMCR_C);
    v &= ~PMCR_D;
    __asm__ __volatile__("mcr p15, 0, %0, c9, c12, 0" : : "r"(v));

    /* Enable the cycle counter itself. */
    v = PMCNTEN_CCNT;
    __asm__ __volatile__("mcr p15, 0, %0, c9, c12, 1" : : "r"(v));

    __asm__ __volatile__("isb" ::: "memory");
}


void pmu_calibrate(uint32_t *read_overhead_cyc, uint32_t *probe_overhead_cyc)
{
    uint32_t best_read  = 0xFFFFFFFFu;
    uint32_t best_probe = 0xFFFFFFFFu;

    /* Back-to-back read cost: the irreducible price of one measurement point. */
    for (int i = 0; i < 64; i++) {
        uint32_t a = pmu_cycles();
        uint32_t b = pmu_cycles();
        uint32_t d = b - a;
        if (d < best_read) best_read = d;
    }

    /* Empty-probe cost: begin + (nothing) + end, minus the read cost. */
    for (int i = 0; i < 64; i++) {
        uint32_t t0 = pmu_cycles();
        uint32_t d  = pmu_cycles() - t0;
        if (d < best_probe) best_probe = d;
    }

    *read_overhead_cyc  = best_read;
    *probe_overhead_cyc = (best_probe > best_read) ? (best_probe - best_read) : 0;
}
