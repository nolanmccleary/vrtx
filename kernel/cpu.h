#ifndef __CPU_H__
#define __CPU_H__

#include <stdint.h>
#include <stdbool.h>
#include "irq.h"
#include "min_heap.h"
#include "thread.h"
#include "thread_fifo.h"


#if ENABLE_SMP == 1
#define NUM_CPUS 2
#else
#define NUM_CPUS 1 
#endif



typedef enum
{
    CPU0 = 0,
    CPU1 = 1,
}   cpu_core_e;



typedef struct
{
    _Alignas(32) volatile bool sched_init;

    //Sched aggregate
    uint32_t ticks;
    uint32_t missed_deadlines;
    uint64_t sum_ci;
    uint64_t sum_ti;
    uint32_t avg_overhead;

    //Sched specific
    heap_t* deadHeap;
    heap_t* relHeap;
    thread_t* curr_thread;
    thread_t* main_thread;
    thread_fifo_t* incoming_threads;

}   cpu_t;


extern volatile uint32_t g_cpu_mailbox_uncached;
extern cpu_t g_cpus[NUM_CPUS];



cpu_core_e curr_core(void);
void update_cpu_metrics(cpu_core_e cpu, uint32_t overhead);
void send_cpu_interrupt(cpu_sgi_e interrupt);

#endif

