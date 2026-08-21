#include <stddef.h>
#include <stdint.h>
#include "aux.h"
#include "preempt_sched.h" 
#include "tlsf.h"
#include "system.h"
#include "thread.h"
#include "min_heap.h"
#include "deque.h"
#include "ktrace.h"




static bool sched_init = false;


static heap_t heap1;
static heap_t heap2;

static heap_t* deadHeap;
static heap_t* relHeap;

static thread_t* curr_thread;
static thread_t* main_thread;


static deque_t* incomingThreads;


static void thread_exit()
{
    curr_thread->thread_status = FINISHED;
    switch_out(curr_thread);
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




uint32_t gTicks;
uint32_t gMissedDeadlines;



// static inline void swap_ptrs(void** p1, void** p2)
// {
//     void* tmp = *p1;
//     *p1 = *p2;
//     *p2 = tmp;
// }



sys_exit_e psched_init()
{
    __asm__ __volatile__("cpsid i" ::: "memory");

    gTicks = 0;
    gMissedDeadlines = 0;

    deadHeap = &heap1;
    relHeap = &heap2;

    incomingThreads = initialize_deque();

    main_thread = (thread_t*)kMalloc(sizeof(thread_t));
    main_thread->thread_status = RUNNING;

    __asm__ __volatile__ ("mov %0, sp" : "=r"(main_thread->sp));
    
    curr_thread = main_thread;

    sched_init = true;

    __asm__ __volatile__("dmb sy" ::: "memory");
    __asm__ __volatile__("cpsie i" ::: "memory");

    return SYS_OK;
}


//TODO: May want to make this only callable from main
sys_exit_e psched_deinit()
{
    __asm__ __volatile__("cpsid i" ::: "memory");

    destroy_deque(incomingThreads);


    for (size_t i = 0; i < deadHeap->curr_index; i++)
    {
        if (deadHeap->heap[i].thread != NULL) kFree(deadHeap->heap[i].thread);
    }

    for (size_t i = 0; i < relHeap->curr_index; i++)
    {
        if (relHeap->heap[i].thread != NULL) kFree(relHeap->heap[i].thread);
    }


    kFree(main_thread);

    sched_init = false;

    __asm__ __volatile__("dmb sy" ::: "memory");
    __asm__ __volatile__("cpsie i" ::: "memory");

    return SYS_OK;
}



sys_exit_e add_thread(sys_exit_e (*func)(void), uint32_t period, thread_periodicity_e periodicity, thread_handle_t* handle)
{
    thread_t* new_thread = (thread_t*)kMalloc(sizeof(thread_t));
    new_thread->period = period;
    new_thread->periodicity = periodicity;

    new_thread->func = func;
    new_thread->sp = (char*)(((uintptr_t)(new_thread->stack + THREAD_STACK_SIZE)) & ~(uintptr_t)0x7); //8-byte align sp so processor doesn't abort

    push_back(incomingThreads, (char*)(new_thread), sizeof(thread_t));

    init_metrics(new_thread);

    handle->thread = new_thread;

    return SYS_OK;
}




sys_exit_e kill_thread(thread_handle_t* handle)
{
    __asm__ __volatile__("cpsid i" ::: "memory");

    handle->thread->periodicity = APERIODIC;
    handle->thread->thread_status = FINISHED;
    if (handle->thread == curr_thread)
    {
        next_thread();
    }

    __asm__ __volatile__("cpsie i" ::: "memory");

    return SYS_OK;
}




sys_exit_e psched_clear_threads(void)
{
    thread_t *thread;

    while (incomingThreads->size > 0)
    {
        size_t size;
        pop_front(incomingThreads, (char**)&thread, &size);
        if (thread != NULL) 
            kFree(thread);
    }

    while (deadHeap->curr_index > 0)
    {
        pop_heap(deadHeap, &thread);
        if (thread != NULL) 
            kFree(thread);
    }

    while (relHeap->curr_index > 0)
    {
        pop_heap(relHeap, &thread);
        if (thread != NULL)
            kFree(thread);
    }

    curr_thread = main_thread;

    return SYS_OK;
}







inline void next_thread()
{

    if (sched_init)
    {
        KTRACE_SCHED_BEGIN();   /* test-only: start per-tick scheduler-cost timer */

        if (curr_thread->thread_status == RUNNING) switch_out(curr_thread);

        gTicks++;

        __asm__ __volatile__ (
            "cps #0x1F\n"
            "mov %0, sp\n"
            "cps #0x13\n"
            : "=r"(curr_thread->sp)
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
            curr_thread = main_thread;

            __asm__ __volatile__ (
                "cps #0x1F\n"
                "mov sp, %0\n"
                "cps #0x13\n"
                :
                : "r"(main_thread->sp)
                : "memory"
            );

            return;
        }
#endif


        while(incomingThreads->size > 0)
        {
            thread_t* thread;
            size_t a;
            pop_front(incomingThreads, (char**)(&thread), &a);

            thread->release_time = gTicks;
            thread->deadline = gTicks + thread->period;
            thread->dirty = false;
            thread->thread_status = PENDING;

            insert_node(deadHeap, thread, thread->deadline);
        }


        bool thread_found = false;

        while (relHeap->curr_index > 0 && geq_wrapped(gTicks, relHeap->heap[0].thread->release_time))
        {
            thread_t* thread;
            pop_heap(relHeap, &thread);
            thread->dirty = false;
            thread->thread_status = PENDING;
            insert_node(deadHeap, thread, thread->deadline);
        }


        while(deadHeap->curr_index > 0) //Find next runnable task, cache locked tasks on a deque before reinserting.
        {
            thread_t* thread;
            pop_heap(deadHeap, &thread);

            if (thread->thread_status == FINISHED)
            {
                if (thread->periodicity == PERIODIC)
                {
                    do 
                    {
                        thread->deadline += thread->period;
                    }   while (thread->deadline <= gTicks);

                    do 
                    {
                        thread->release_time += thread->period;
                    }   while (thread->release_time < thread->deadline - thread->period);

                    if (geq_wrapped(gTicks, thread->release_time)) //Will nominally fire on equality
                    {
                        thread->dirty = false;
                        thread->thread_status = PENDING;
                    }

                    else //Add to release heap
                    {
                        insert_node(relHeap, thread, thread->release_time);
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
                if (geq_wrapped(gTicks, thread->deadline) && !thread->dirty)
                {
                    thread->dirty = true;
                    gMissedDeadlines++;
                }

                curr_thread = thread;
                thread_found = true;
                insert_node(deadHeap, thread, thread->deadline);
                break;
            }
        }


        if (!thread_found) //Either no valid tasks set or we ran the last one last cycle
        {
            curr_thread = main_thread;
        }


        switch_in(curr_thread);

        KTRACE_SCHED_END();   /* test-only: fold this tick's scheduler cost into g_metrics[SCHED_METRIC] */

        KTRACE_TICK_EXIT(curr_thread);   /* test-only per-tick hook (Gantt + metrics mirror) */

        switch (curr_thread->thread_status)
        {
            case PENDING:
                prime_thread(curr_thread);
                /* fall through */

            case RUNNING:
                __asm__ __volatile__ (
                    "cps #0x1F\n"
                    "mov sp, %0\n"
                    "cps #0x13\n"
                    :
                    : "r"(curr_thread->sp)
                );
                break;

            default:
                break;
        }
    }
}


