#include "gic.h"
#include "cpu.h"
#include "aux.h"
#include "telemetry.h"
#include "fault.h"


HOST_SHARED_OCRAM volatile uint32_t g_cpu_mailbox_uncached;


cpu_t g_cpus[NUM_CPUS];



inline cpu_core_e curr_core(void)
{
    uint32_t mpidr;
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
    return (cpu_core_e)(mpidr & 0xFF);
}


inline void update_cpu_scheduler_overhead(cpu_core_e cpu, uint32_t overhead)
{
    g_cpus[cpu].avg_scheduler_overhead = overhead - (overhead >> ALPHA) + (g_cpus[cpu].avg_scheduler_overhead >> ALPHA);
}


inline bool add_thread_to_upool(thread_t* thread)
{
    cpu_core_e core = thread->core;

    if (g_cpus[core].last >= MAX_THREADS) raise_error("UPOOL FULL");

    thread->id = g_cpus[core].last;
    g_cpus[core].upool[g_cpus[core].last] = thread;
    g_cpus[core].last++;
    return true;
}


inline void remove_thread_from_upool(thread_t* thread)
{
    cpu_core_e core = thread->core;

    g_cpus[core].last--;
    thread_t* last = g_cpus[core].upool[g_cpus[core].last];
    g_cpus[core].upool[thread->id] = last;
    last->id = thread->id;
}




void update_upool(thread_t* thread)
{
    cpu_core_e core = thread->core;

    uint32_t u = 0;
    for (size_t i = 0; i < g_cpus[core].last; i++)
    {
        thread_t* t = g_cpus[core].upool[i];
        if (t->periodicity == APERIODIC) continue;
        if (t->metrics.ti_av) u += (uint32_t)(((uint64_t)t->metrics.ci_av << 16) / t->metrics.ti_av);
    }

    g_cpus[core].utilization = u;
}



inline void send_cpu_interrupt(cpu_sgi_e interrupt)
{
    cpu_core_e core = curr_core();
    __asm__ volatile ("dmb sy" ::: "memory");
    GICD_SGIR = (1u << (16u + (core ^ 0x1u)))   // CPUTargetList [23:16]: the other core
              | ((uint32_t)interrupt & 0xFu);    // SGI ID [3:0]
}
