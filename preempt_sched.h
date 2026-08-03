#ifndef __PREEMPT_SCHED_H__
#define __PREEMPT_SCHED_H__

#include "system.h"
#include "thread.h"



void psched_init(void);
void psched_deinit(void);
sys_exit_e add_thread(sys_exit_e (*func)(thread_status_e* status), thread_crit_e crit, uint32_t id);
sys_exit_e free_thread(uint32_t id);
void clean_pool(void);
extern void next_thread(void);



#endif
