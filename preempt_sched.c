#include <stddef.h>
#include <stdint.h>
#include "preempt_sched.h" 
#include "allocator.h"
#include "flags.h"


#define MAX_THREADS 64



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



sys_exit_e free_thread(uint32_t id)
{
    for(size_t i = 0; i < MAX_THREADS; i++)
    {
        thread_wrapper_t* thread = &thread_pool[i];
        if(thread->available == UNAVAILABLE && thread->id == id)
        {
            kFree(thread->thread);
            thread->thread = NULL;
            thread->available = AVAILABLE;
            num_threads--;
            return SYS_OK;
        }
    }

    FLAG_WRITE(NUM_THREADS, num_threads);

    return SYS_ERROR;
}



void clean_pool(void)
{
    for(size_t i = 0; i < MAX_THREADS; i++)
    {
        thread_wrapper_t* thread = &thread_pool[i];
        if(thread->available == UNAVAILABLE && thread->thread->thread_status == FINISHED)
        {
            thread_t* to_free = thread->thread;
            thread->available = AVAILABLE;
            thread->thread = NULL;
            num_threads--;
            num_running--;
            kFree(to_free);
        }
    }

    FLAG_WRITE(NUM_THREADS, num_threads);
    FLAG_WRITE(NUM_RUNNING, num_running);
}



static inline void start_thread(thread_t* thread)
{
    uint32_t* sp = (uint32_t*)thread->sp;

    // RFEFD frame (bottom)
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







