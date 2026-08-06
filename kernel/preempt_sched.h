#ifndef __PREEMPT_SCHED_H__
#define __PREEMPT_SCHED_H__

#include <stdint.h>
#include "system.h"
#include "thread.h"



void psched_init(void);
void psched_deinit(void);
sys_exit_e add_thread(sys_exit_e (*func)(thread_status_e* status), uint32_t period, thread_periodicity_e periodicity);
extern void next_thread(void);



#endif
