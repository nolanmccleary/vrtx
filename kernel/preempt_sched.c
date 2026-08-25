#include <stddef.h>
#include <stdint.h>
#include "aux.h"
#include "cpu.h"
#include "lock.h"
#include "pmu.h"
#include "preempt_sched.h"
#include "tlsf.h"
#include "system.h"
#include "thread.h"
#include "min_heap.h"
#include "thread_fifo.h"
#include "ktrace.h"




static heap_t heap1[NUM_CPUS];
static heap_t heap2[NUM_CPUS];
static thread_fifo_t incoming_fifos[NUM_CPUS];



static void thread_exit()
{
    cpu_core_e core = curr_core();

    g_cpus[core].curr_thread->thread_status = FINISHED;
    switch_out(g_cpus[core].curr_thread);
    for (;;) {};
}




static inline void prime_thread(thread_t* thread)
{
    uint32_t* sp = (uint32_t*)(((uintptr_t)(thread->stack + THREAD_STACK_SIZE)) & ~(uintptr_t)0x7);

    //Return From Exception Full Descending (RFEFD) frame (bottom) as well as all the popping stuff etc
    *(--sp) = MODE_SYS;                    // spsr_irq
    *(--sp) = (uint32_t)thread->func;      // lr_irq → pc

    // r0-r12 (full integer file; the IRQ handler saves/restores all of them)
    *(--sp) = 0; // r12
    *(--sp) = 0; // r11
    *(--sp) = 0; // r10
    *(--sp) = 0; // r9
    *(--sp) = 0; // r8
    *(--sp) = 0; // r7
    *(--sp) = 0; // r6
    *(--sp) = 0; // r5
    *(--sp) = 0; // r4
    *(--sp) = 0; // r3
    *(--sp) = 0; // r2
    *(--sp) = 0; // r1
    *(--sp) = 0; //(uint32_t)&thread->thread_status; // r0

    uint32_t adjustment = ((uint32_t)sp) & 4;
    sp = (uint32_t*)((uint32_t)sp - adjustment);

    *(--sp) = (uint32_t)thread_exit; //lr
    *(--sp) = adjustment; //store adjustment

    thread->sp = (char*)sp;
    thread->thread_status = RUNNING;
}






// static inline void swap_ptrs(void** p1, void** p2)
// {
//     void* tmp = *p1;
//     *p1 = *p2;
//     *p2 = tmp;
// }



sys_exit_e psched_core_init(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
    cpu_core_e core = curr_core();

    char* sp;
    __asm__ __volatile__ ("mov %0, sp" : "=r"(sp));
    g_cpus[core].ticks = 0;
    g_cpus[core].missed_deadlines = 0;

    g_cpus[core].deadHeap = &heap1[core];
    g_cpus[core].relHeap = &heap2[core];
    g_cpus[core].incoming_threads = &incoming_fifos[core];

    initialize_fifo(g_cpus[core].incoming_threads);

    g_cpus[core].main_thread = (thread_t*)kMalloc(sizeof(thread_t));
    g_cpus[core].main_thread->thread_status = RUNNING;
    g_cpus[core].main_thread->sp = sp;

    g_cpus[core].curr_thread = g_cpus[core].main_thread;

    g_cpus[core].sched_init = true;

    __asm__ __volatile__("dmb sy" ::: "memory");

    __asm__ __volatile__("cpsie i" ::: "memory");
    return SYS_OK;
}



sys_exit_e psched_core_deinit(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
    cpu_core_e core = curr_core();


    destroy_threads(g_cpus[core].incoming_threads);

    for (size_t i = 0; i < g_cpus[core].deadHeap->curr_index; i++)
    {
        if (g_cpus[core].deadHeap->heap[i].thread != NULL) kFree(g_cpus[core].deadHeap->heap[i].thread);
    }

    for (size_t i = 0; i < g_cpus[core].relHeap->curr_index; i++)
    {
        if (g_cpus[core].relHeap->heap[i].thread != NULL) kFree(g_cpus[core].relHeap->heap[i].thread);
    }

    kFree(g_cpus[core].main_thread);

    g_cpus[core].sched_init = false;

    __asm__ __volatile__("dmb sy" ::: "memory");
    __asm__ __volatile__("cpsie i" ::: "memory");
    
    return SYS_OK;
}



sys_exit_e psched_init(void)
{
    psched_core_init();

#if ENABLE_SMP
    uint32_t prev = g_spin_exit;
    g_spin_exit = 0;

    send_cpu_interrupt(CPU_PSCHED_INIT_REQUEST);

    while (!g_spin_exit)
    {
        __asm__ volatile ("wfi");
    };

    g_spin_exit = prev;
#endif
    return SYS_OK;
}



sys_exit_e psched_deinit(void)
{
    psched_core_deinit();

#if ENABLE_SMP
    uint32_t prev = g_spin_exit;
    g_spin_exit = 0;

    send_cpu_interrupt(CPU_PSCHED_DEINIT_REQUEST);

    while (!g_spin_exit)
    {
        __asm__ volatile ("wfi");
    };

    g_spin_exit = prev;
#endif
    return SYS_OK;
}



thread_t* add_thread_to_core(cpu_core_e core, sys_exit_e (*func)(void), uint32_t period, thread_periodicity_e periodicity)
{

    thread_t* new_thread = (thread_t*)kMalloc(sizeof(thread_t));
    new_thread->period = period;
    new_thread->periodicity = periodicity;

    new_thread->func = func;
    new_thread->sp = (char*)(((uintptr_t)(new_thread->stack + THREAD_STACK_SIZE)) & ~(uintptr_t)0x7); //8-byte align sp so processor doesn't abort

    fifo_push(g_cpus[core].incoming_threads, new_thread);

    init_metrics(new_thread);

    return new_thread;
}



sys_exit_e kill_thread(thread_t* thread)
{
    __asm__ __volatile__("cpsid i" ::: "memory");

    cpu_core_e core = curr_core();

    thread->periodicity = APERIODIC;
    thread->thread_status = FINISHED;
    if (thread == g_cpus[core].curr_thread)
    {
        next_thread();
    }

    __asm__ __volatile__("cpsie i" ::: "memory");

    return SYS_OK;
}




sys_exit_e psched_clear_threads(void)
{
    cpu_core_e core = curr_core();

    thread_t *thread;

    destroy_threads(g_cpus[core].incoming_threads);

    while (g_cpus[core].deadHeap->curr_index > 0)
    {
        pop_heap(g_cpus[core].deadHeap, &thread);
        if (thread != NULL)
            kFree(thread);
    }

    while (g_cpus[core].relHeap->curr_index > 0)
    {
        pop_heap(g_cpus[core].relHeap, &thread);
        if (thread != NULL)
            kFree(thread);
    }

    g_cpus[core].curr_thread = g_cpus[core].main_thread;

    return SYS_OK;
}




inline void next_thread()
{
    uint32_t overhead = pmu_cycles();
    cpu_core_e core = curr_core();


    if (g_cpus[core].sched_init)
    {

        if (g_cpus[core].curr_thread->thread_status == RUNNING) switch_out(g_cpus[core].curr_thread);

        g_cpus[core].ticks++;

        __asm__ __volatile__ (
            "cps #0x1F\n"
            "mov %0, sp\n"
            "cps #0x13\n"
            : "=r"(g_cpus[core].curr_thread->sp)
        );


#ifdef MODE_TEST
        /*
        * Python finished sampling the current EDF trial.
        *
        * At overload, EDF may otherwise starve main forever.
        * Force main in once so KTRACE_WAIT_RELEASE() can observe
        * g_test_release and return.
        */
        if (KTRACE_RELEASE_PENDING())
        {
            g_cpus[core].curr_thread = g_cpus[core].main_thread;

            __asm__ __volatile__ (
                "cps #0x1F\n"
                "mov sp, %0\n"
                "cps #0x13\n"
                :
                : "r"(g_cpus[core].main_thread->sp)
                : "memory"
            );

            return;
        }
#endif

        thread_t* thread;

        while(g_cpus[core].incoming_threads->size > 0)
        {
            thread = fifo_pop(g_cpus[core].incoming_threads);

            thread->release_time = g_cpus[core].ticks;
            thread->deadline = g_cpus[core].ticks + thread->period;
            thread->dirty = false;
            thread->thread_status = PENDING;

            insert_node(g_cpus[core].deadHeap, thread, thread->deadline);
        }


        bool thread_found = false;

        while (g_cpus[core].relHeap->curr_index > 0 && geq_wrapped(g_cpus[core].ticks, g_cpus[core].relHeap->heap[0].thread->release_time))
        {
            pop_heap(g_cpus[core].relHeap, &thread);
            thread->dirty = false;
            thread->thread_status = PENDING;
            insert_node(g_cpus[core].deadHeap, thread, thread->deadline);
        }


        while(g_cpus[core].deadHeap->curr_index > 0) //Find next runnable task, cache locked tasks on a deque before reinserting.
        {
            thread = g_cpus[core].deadHeap->heap[0].thread;

            if (thread->thread_status == FINISHED)
            {
                pop_heap(g_cpus[core].deadHeap, &thread);

                if (thread->periodicity == PERIODIC)
                {
                    do
                    {
                        thread->deadline += thread->period;
                    }   while (thread->deadline <= g_cpus[core].ticks);

                    do
                    {
                        thread->release_time += thread->period;
                    }   while (thread->release_time < thread->deadline - thread->period);

                    if (geq_wrapped(g_cpus[core].ticks, thread->release_time)) //Will nominally fire on equality
                    {
                        thread->dirty = false;
                        thread->thread_status = PENDING;
                    }

                    else //Add to release heap
                    {
                        insert_node(g_cpus[core].relHeap, thread, thread->release_time);
                    }

                }

                else
                {
                    kFree(thread);
                    continue;
                }

            }

            if(thread->thread_status == PENDING || thread->thread_status == RUNNING)
            {
                if (geq_wrapped(g_cpus[core].ticks, thread->deadline) && !thread->dirty)
                {
                    thread->dirty = true;
                    g_cpus[core].missed_deadlines++;
                }

                g_cpus[core].curr_thread = thread;
                thread_found = true;
                break;
            }
        }


        if (!thread_found) //Either no valid tasks set or we ran the last one last cycle
        {
            g_cpus[core].curr_thread = g_cpus[core].main_thread;
        }


        switch_in(g_cpus[core].curr_thread);


        KTRACE_TICK_EXIT(g_cpus[core].curr_thread);   /* test-only per-tick hook (Gantt + metrics mirror) */

        switch (g_cpus[core].curr_thread->thread_status)
        {
            case PENDING:
                prime_thread(g_cpus[core].curr_thread);
                /* fall through */

            case RUNNING:
                __asm__ __volatile__ (
                    "cps #0x1F\n"
                    "mov sp, %0\n"
                    "cps #0x13\n"
                    :
                    : "r"(g_cpus[core].curr_thread->sp)
                );
                break;

            default:
                break;
        }
    }



    overhead = pmu_cycles() - overhead;
    update_cpu_metrics(core, overhead);
}
