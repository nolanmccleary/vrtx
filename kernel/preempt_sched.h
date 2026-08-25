#ifndef __PREEMPT_SCHED_H__
#define __PREEMPT_SCHED_H__

#include <stdint.h>
#include "system.h"
#include "thread.h"
#include "cpu.h"



sys_exit_e psched_core_init(void);
sys_exit_e psched_core_deinit(void);
sys_exit_e psched_init(void);
sys_exit_e psched_deinit(void);
sys_exit_e psched_clear_threads(void);

thread_t* add_thread_to_core(cpu_core_e core, sys_exit_e (*func)(void), uint32_t period, thread_periodicity_e periodicity);
sys_exit_e kill_thread(thread_t* thread); //assumed called from same core for now



void next_thread(void);



#endif
