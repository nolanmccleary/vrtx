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
#include "deque.h"
#include "ktrace.h"




volatile bool sched_init[NUM_CPUS];


static heap_t heap1[NUM_CPUS];
static heap_t heap2[NUM_CPUS];

static heap_t* deadHeap[NUM_CPUS];
static heap_t* relHeap[NUM_CPUS];

static thread_t* curr_thread[NUM_CPUS];
static thread_t* main_thread[NUM_CPUS];


static deque_t* incomingThreads[NUM_CPUS];


static void thread_exit()
{
    cpu_e core = curr_core();

    curr_thread[core]->thread_status = FINISHED;
    switch_out(curr_thread[core]);
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




uint32_t gTicks[NUM_CPUS];
uint32_t gMissedDeadlines[NUM_CPUS];



// static inline void swap_ptrs(void** p1, void** p2)
// {
//     void* tmp = *p1;
//     *p1 = *p2;
//     *p2 = tmp;
// }



sys_exit_e psched_core_init(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
    cpu_e core = curr_core();

    char* sp;
    __asm__ __volatile__ ("mov %0, sp" : "=r"(sp));
    gTicks[core] = 0;
    gMissedDeadlines[core] = 0;

    deadHeap[core] = &heap1[core];
    relHeap[core] = &heap2[core];

    incomingThreads[core] = initialize_deque();

    main_thread[core] = (thread_t*)kMalloc(sizeof(thread_t));
    main_thread[core]->thread_status = RUNNING;
    main_thread[core]->sp = sp;

    curr_thread[core] = main_thread[core];

    sched_init[core] = true;

    __asm__ __volatile__("dmb sy" ::: "memory");

    __asm__ __volatile__("cpsie i" ::: "memory");
    return SYS_OK;
}



sys_exit_e psched_core_deinit(void)
{
    __asm__ __volatile__("cpsid i" ::: "memory");
    cpu_e core = curr_core();


    destroy_deque(incomingThreads[core]);

    for (size_t i = 0; i < deadHeap[core]->curr_index; i++)
    {
        if (deadHeap[core]->heap[i].thread != NULL) kFree(deadHeap[core]->heap[i].thread);
    }

    for (size_t i = 0; i < relHeap[core]->curr_index; i++)
    {
        if (relHeap[core]->heap[i].thread != NULL) kFree(relHeap[core]->heap[i].thread);
    }

    kFree(main_thread[core]);

    sched_init[core] = false;

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



sys_exit_e add_thread_to_core(cpu_e core, sys_exit_e (*func)(void), uint32_t period, thread_periodicity_e periodicity, thread_handle_t* handle)
{

    thread_t* new_thread = (thread_t*)kMalloc(sizeof(thread_t));
    new_thread->period = period;
    new_thread->periodicity = periodicity;

    new_thread->func = func;
    new_thread->sp = (char*)(((uintptr_t)(new_thread->stack + THREAD_STACK_SIZE)) & ~(uintptr_t)0x7); //8-byte align sp so processor doesn't abort

    push_back(incomingThreads[core], (char*)(new_thread), sizeof(thread_t));

    init_metrics(new_thread);

    handle->thread = new_thread;

    return SYS_OK;
}



sys_exit_e kill_thread(thread_handle_t* handle)
{
    __asm__ __volatile__("cpsid i" ::: "memory");

    cpu_e core = curr_core();

    handle->thread->periodicity = APERIODIC;
    handle->thread->thread_status = FINISHED;
    if (handle->thread == curr_thread[core])
    {
        next_thread();
    }

    __asm__ __volatile__("cpsie i" ::: "memory");

    return SYS_OK;
}




sys_exit_e psched_clear_threads(void)
{
    cpu_e core = curr_core();

    thread_t *thread;

    while (incomingThreads[core]->size > 0)
    {
        size_t size;
        pop_front(incomingThreads[core], (char**)&thread, &size);
        if (thread != NULL)
            kFree(thread);
    }

    while (deadHeap[core]->curr_index > 0)
    {
        pop_heap(deadHeap[core], &thread);
        if (thread != NULL)
            kFree(thread);
    }

    while (relHeap[core]->curr_index > 0)
    {
        pop_heap(relHeap[core], &thread);
        if (thread != NULL)
            kFree(thread);
    }

    curr_thread[core] = main_thread[core];

    return SYS_OK;
}




inline void next_thread()
{
    uint32_t overhead = pmu_cycles();
    cpu_e core = curr_core();


    if (sched_init[core])
    {

        if (curr_thread[core]->thread_status == RUNNING) switch_out(curr_thread[core]);

        gTicks[core]++;

        __asm__ __volatile__ (
            "cps #0x1F\n"
            "mov %0, sp\n"
            "cps #0x13\n"
            : "=r"(curr_thread[core]->sp)
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
            curr_thread[core] = main_thread[core];

            __asm__ __volatile__ (
                "cps #0x1F\n"
                "mov sp, %0\n"
                "cps #0x13\n"
                :
                : "r"(main_thread[core]->sp)
                : "memory"
            );

            return;
        }
#endif

        thread_t* thread;

        while(incomingThreads[core]->size > 0)
        {
            size_t a;
            pop_front(incomingThreads[core], (char**)(&thread), &a);

            thread->release_time = gTicks[core];
            thread->deadline = gTicks[core] + thread->period;
            thread->dirty = false;
            thread->thread_status = PENDING;

            insert_node(deadHeap[core], thread, thread->deadline);
        }


        bool thread_found = false;

        while (relHeap[core]->curr_index > 0 && geq_wrapped(gTicks[core], relHeap[core]->heap[0].thread->release_time))
        {
            pop_heap(relHeap[core], &thread);
            thread->dirty = false;
            thread->thread_status = PENDING;
            insert_node(deadHeap[core], thread, thread->deadline);
        }


        while(deadHeap[core]->curr_index > 0) //Find next runnable task, cache locked tasks on a deque before reinserting.
        {
            thread = deadHeap[core]->heap[0].thread;

            if (thread->thread_status == FINISHED)
            {
                pop_heap(deadHeap[core], &thread);

                if (thread->periodicity == PERIODIC)
                {
                    do
                    {
                        thread->deadline += thread->period;
                    }   while (thread->deadline <= gTicks[core]);

                    do
                    {
                        thread->release_time += thread->period;
                    }   while (thread->release_time < thread->deadline - thread->period);

                    if (geq_wrapped(gTicks[core], thread->release_time)) //Will nominally fire on equality
                    {
                        thread->dirty = false;
                        thread->thread_status = PENDING;
                    }

                    else //Add to release heap
                    {
                        insert_node(relHeap[core], thread, thread->release_time);
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
                if (geq_wrapped(gTicks[core], thread->deadline) && !thread->dirty)
                {
                    thread->dirty = true;
                    gMissedDeadlines[core]++;
                }

                curr_thread[core] = thread;
                thread_found = true;
                break;
            }
        }


        if (!thread_found) //Either no valid tasks set or we ran the last one last cycle
        {
            curr_thread[core] = main_thread[core];
        }


        switch_in(curr_thread[core]);


        KTRACE_TICK_EXIT(curr_thread[core]);   /* test-only per-tick hook (Gantt + metrics mirror) */

        switch (curr_thread[core]->thread_status)
        {
            case PENDING:
                prime_thread(curr_thread[core]);
                /* fall through */

            case RUNNING:
                __asm__ __volatile__ (
                    "cps #0x1F\n"
                    "mov sp, %0\n"
                    "cps #0x13\n"
                    :
                    : "r"(curr_thread[core]->sp)
                );
                break;

            default:
                break;
        }
    }



    overhead = pmu_cycles() - overhead;
    update_cpu_metrics(core, overhead);
}
