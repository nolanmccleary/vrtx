#ifndef __PREEMPT_SCHED_H__
#define __PREEMPT_SCHED_H__

#include <stdint.h>
#include "system.h"
#include "thread.h"


extern uint32_t gTicks;
extern uint32_t gMissedDeadlines;


sys_exit_e psched_init(void);
sys_exit_e psched_deinit(void);
sys_exit_e psched_clear_threads(void);
sys_exit_e add_thread(sys_exit_e (*func)(void), uint32_t period, thread_periodicity_e periodicity);
void next_thread(void);



#endif
