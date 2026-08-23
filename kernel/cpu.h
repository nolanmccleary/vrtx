#ifndef __CPU_H__
#define __CPU_H__

#include <stdint.h>
#include "system.h"   /* ALPHA (used by update_cpu_metrics below) */


#if ENABLE_SMP == 1
#define NUM_CPUS 2
#else
#define NUM_CPUS 1 
#endif



typedef enum
{
    CPU0 = 0,
    CPU1 = 1,
}   cpu_e;


inline cpu_e curr_core(void)
{
    uint32_t mpidr;
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
    return (cpu_e)(mpidr & 0xFF);
}


typedef struct 
{
    uint64_t sum_ci;
    uint64_t sum_ti;
    uint32_t avg_overhead;
}   cpu_t;


extern volatile uint32_t g_cpu_mailbox;
extern cpu_t g_cpus[NUM_CPUS];


inline void update_cpu_metrics(cpu_e cpu, uint32_t overhead)
{
    g_cpus[cpu].avg_overhead = overhead - (overhead >> ALPHA) + (g_cpus[cpu].avg_overhead >> ALPHA);
}


#endif

