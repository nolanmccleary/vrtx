#ifndef __PREEMPT_SCHED_H__
#define __PREEMPT_SCHED_H__

#include <stdint.h>
#include "system.h"
#include "thread.h"
#include "cpu.h"



typedef struct 
{
    thread_t* thread;
}   thread_handle_t;




extern volatile bool sched_init[NUM_CPUS];

extern uint32_t gTicks[NUM_CPUS];
extern uint32_t gMissedDeadlines[NUM_CPUS];


sys_exit_e psched_core_init(void);
sys_exit_e psched_core_deinit(void);
sys_exit_e psched_init(void);
sys_exit_e psched_deinit(void);
sys_exit_e psched_clear_threads(void);

sys_exit_e add_thread(sys_exit_e (*func)(void), uint32_t period, thread_periodicity_e periodicity, thread_handle_t* handle);
sys_exit_e kill_thread(thread_handle_t* handle);



void next_thread(void);



#endif
