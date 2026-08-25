#ifndef __CPU_H__
#define __CPU_H__

#include <stdint.h>
#include "irq.h"



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




typedef struct 
{
    uint64_t sum_ci;
    uint64_t sum_ti;
    uint32_t avg_overhead;
}   cpu_t;


extern volatile uint32_t g_cpu_mailbox_uncached;
extern volatile cpu_t g_cpus[NUM_CPUS];



cpu_e curr_core(void);
void update_cpu_metrics(cpu_e cpu, uint32_t overhead);
void send_cpu_interrupt(cpu_sgi_e interrupt);

#endif

