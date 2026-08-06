#include <stddef.h>
#include <stdint.h>
#include "aux.h"
#include "preempt_sched.h" 
#include "allocator.h"
#include "flags.h"
#include "system.h"
#include "thread.h"
#include "min_heap.h"
#include "deque.h"




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
    for (;;) {};
}


static inline void start_thread(thread_t* thread)
{
    uint32_t* sp = (uint32_t*)thread->sp;

    //Return From Exception Full Descending (RFEFD) frame (bottom) as well as all the popping stuff etc
    *(--sp) = MODE_SYS;                    // spsr_irq
    *(--sp) = (uint32_t)thread->func;      // lr_irq → pc

    // r0-r3, r12
    *(--sp) = 0; // r12
    *(--sp) = 0; // r3
    *(--sp) = 0; // r2
    *(--sp) = 0; // r1
    *(--sp) = (uint32_t)&thread->thread_status; // r0

    uint32_t adjustment = ((uint32_t)sp) & 4;
    sp = (uint32_t*)((uint32_t)sp - adjustment);

    *(--sp) = (uint32_t)thread_exit;
    *(--sp) = adjustment; // alignment

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



void psched_init()
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
}


void psched_deinit()
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

    __asm__ __volatile__ ( 
        "cps #0x1F\n"
        "mov sp, %0\n"
        "cps #0x13\n"
        :
        : "r"(main_thread->sp)
    );

    kFree(main_thread);

    sched_init = false;

    __asm__ __volatile__("dmb sy" ::: "memory");
    __asm__ __volatile__("cpsie i" ::: "memory");
}



sys_exit_e add_thread(sys_exit_e (*func)(thread_status_e* status), uint32_t period, thread_periodicity_e periodicity)
{
    thread_t* new_thread = (thread_t*)kMalloc(sizeof(thread_t));
    new_thread->period = period;
    new_thread->periodicity = periodicity;

    new_thread->func = func;
    /* Round down to an 8-byte boundary: -fshort-enums can place stack[] at a
       non-word offset in thread_t, which would leave sp misaligned and fault
       the first STM/STR in start_thread / the thread prologue (AAPCS wants 8). */
    new_thread->sp = (char*)(((uintptr_t)(new_thread->stack + THREAD_STACK_SIZE)) & ~(uintptr_t)0x7);
    

    push_back(incomingThreads, (char*)(new_thread), sizeof(thread_t));

    return SYS_OK;
}



inline void next_thread()
{

    if (sched_init)
    {
        gTicks++;

        __asm__ __volatile__ (
            "cps #0x1F\n"
            "mov %0, sp\n"
            "cps #0x13\n"
            : "=r"(curr_thread->sp)
        );


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


        switch (curr_thread->thread_status)
        {
                case PENDING:
                    start_thread(curr_thread);
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


