#include <stddef.h>
#include <stdint.h>
#include "preempt_sched.h" 
#include "allocator.h"
#include "flags.h"
#include "system.h"
#include "thread.h"
#include "deque.h"
#include "min_heap.h"









#ifdef LINEAR_SCHED //Keeping this so we can profile later

#define MAX_THREADS 64

static int num_running = 0;


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

    *(--sp) = 0; // lr_sys
    *(--sp) = adjustment; // alignment

    thread->sp = (char*)sp;
    thread->thread_status = RUNNING;

    num_running++;
    FLAG_WRITE(NUM_RUNNING, num_running);
}


typedef enum
{
    AVAILABLE,
    UNAVAILABLE,
    FLAGGED,
}   thread_availability_e;


typedef struct
{
    thread_t* thread;
    thread_availability_e available;
    uint32_t id;
}   thread_wrapper_t;


static bool scheduler_initialized = false;
static thread_wrapper_t thread_pool[MAX_THREADS];
uint32_t num_threads = 0;
uint32_t num_running = 0;


static uint32_t ticks = 0;
static thread_t* curr_thread = NULL;
static char* main_sp = NULL;
static size_t curr_thread_index = 0;



void psched_init(void)
{
    for(size_t i = 0; i < MAX_THREADS; i++)
    {
        thread_pool[i].available = AVAILABLE;
        thread_pool[i].thread = NULL;
    }
    
    ticks = 0;
    num_threads = 0;
    num_running = 0;
    curr_thread_index = 0;
    
    main_sp = NULL;
    
    scheduler_initialized = true;
    curr_thread = NULL;
}


void psched_deinit(void)
{
    for(size_t i = 0; i < MAX_THREADS; i++)
    {
        if(thread_pool[i].thread != NULL)
        {
            kFree(thread_pool[i].thread);
            thread_pool[i].thread = NULL;
            thread_pool[i].available = AVAILABLE;
        }
    }

    num_threads = 0;
    curr_thread = NULL;
    scheduler_initialized = false;
}


sys_exit_e add_thread(sys_exit_e (*func)(thread_status_e* status), thread_crit_e crit, uint32_t id)
{
    if(!scheduler_initialized) psched_init();

    if(num_threads < MAX_THREADS)
    {
        for(size_t i = 0; i < MAX_THREADS; i++)
        {
            if(thread_pool[i].available == AVAILABLE)
            {
                thread_pool[i].available = UNAVAILABLE;
                thread_t* new_thread = (thread_t*)kMalloc(sizeof(thread_t));
                new_thread->thread_status = PENDING;
                new_thread->func = func;
                new_thread->crit = crit;
                new_thread->sp = new_thread->stack + THREAD_STACK_SIZE;
                thread_pool[i].thread = new_thread;

                thread_pool[i].id = id;
                num_threads++;
                FLAG_WRITE(NUM_THREADS, num_threads);
                return SYS_OK;
            }
        }
    }
    
    FLAG_WRITE(NUM_THREADS, num_threads);

    return SYS_ERROR;
}



inline void next_thread(void)
{
    if (num_threads > 0)
    {
        if (ticks == 0)  //we were on main before; update main sp
        {
            __asm__ __volatile__ (
                "cps #0x1F\n"
                "mov %0, sp\n"
                "cps #0x13\n"
                : "=r"(main_sp)
            );
        }

        else  //update old curr_thread's sp
        {
            __asm__ __volatile__ (
                "cps #0x1F\n"
                "mov %0, sp\n"
       "cps #0x13\n"
                : "=r"(curr_thread->sp)
            );
        }

        ticks++;

        if (ticks == num_threads + 1) //next thread is main; set sp to main sp
        {
            __asm__ __volatile__ (
                "cps #0x1F\n"
                "mov sp, %0\n"
                "cps #0x13\n"
                :
                : "r"(main_sp)
            );
        }

        else 
        {
            for (size_t i = 1; i < MAX_THREADS; i++)
            {
                size_t index = (curr_thread_index + i) % MAX_THREADS;
                thread_wrapper_t* thread = &thread_pool[index];

                if (thread->available == UNAVAILABLE && thread->thread->thread_status == PENDING) //starting pending should take priority over continuing running
                {
                    curr_thread = thread->thread;
                    curr_thread_index = index;
                    break;

                }

                if (thread->available == UNAVAILABLE && thread->thread->thread_status == RUNNING)
                {
                    curr_thread = thread->thread;
                    curr_thread_index = index;
                    break;
                }
            }


            switch (curr_thread->thread_status)
            {
                case FINISHED: //couldn't find any currently running threads or new pending threads to start and our last thread just finished so we should go back to main.
                    // thread_pool[curr_thread_index].available = AVAILABLE;
                    // kFree(curr_thread);
                    // num_threads--;
                    __asm__ __volatile__ ( 
                        "cps #0x1F\n"
                        "mov sp, %0\n"
                        "cps #0x13\n"
                        :
                        : "r"(main_sp)
                    );
                    break;

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

        ticks %= (num_threads + 1);


    } //if num_threads > 0
}


#else //Default to EDF scheduler










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

    *(--sp) = 0; // lr_sys
    *(--sp) = adjustment; // alignment

    thread->sp = (char*)sp;
    thread->thread_status = RUNNING;
}




uint32_t gTicks;
uint32_t gMissedDeadlines;

static inline bool passed_deadline(uint32_t ticker, uint32_t deadline)
{
    return ((int32_t)ticker - (int32_t)deadline >= 0);
}


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

    curr_thread = main_thread;

    sched_init = true;

    __asm__ __volatile__("dmb sy" ::: "memory");
    __asm__ __volatile__("cpsie i" ::: "memory");
}


void psched_deinit()
{
    __asm__ __volatile__("cpsid i" ::: "memory");

    destroy_deque(incomingThreads);


    for (size_t i = 0; i < MAX_NODES; i++)
    {
        if (deadHeap->heap[i].thread != NULL) kFree(deadHeap->heap[i].thread);
        if (relHeap->heap[i].thread != NULL) kFree(relHeap->heap[i].thread);
    }


    kFree(curr_thread);     //Free memory occupied by thread objects
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
            thread->thread_status = PENDING;

            insert_node(deadHeap, thread, thread->deadline);
        }


        bool thread_found = false;

        while (relHeap->curr_index > 0 && passed_deadline(gTicks, relHeap->heap[0].thread->release_time))
        {
            thread_t* thread;
            pop_heap(relHeap, &thread);
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

                    if (passed_deadline(gTicks, thread->release_time)) //Will nominally fire on equality
                    {
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
                }

            }

            if(thread->thread_status == PENDING || thread->thread_status == RUNNING)
            {
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






































#endif




