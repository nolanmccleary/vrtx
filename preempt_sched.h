#ifndef __PREEMPT_SCHED_H__
#define __PREEMPT_SCHED_H__

#include "system.h"

#define THREAD_STACK_SIZE 0x2000
#define MODE_SYS          0x1F

typedef enum
{
    PENDING,
    RUNNING,
    FINISHED,
}   thread_status_e;


typedef enum
{
    LOW,
    MEDIUM,
    HIGH,
}   thread_crit_e;

typedef enum
{
    AVAILABLE,
    UNAVAILABLE,
    FLAGGED,
}   thread_availability_e;


typedef struct
{
    char stack[THREAD_STACK_SIZE];
    char* sp;
    thread_status_e thread_status;
    sys_exit_e (*func)(thread_status_e* status);
    thread_crit_e crit;
}   thread_t;


void psched_init(void);
void psched_deinit(void);
sys_exit_e add_thread(sys_exit_e (*func)(thread_status_e* status), thread_crit_e crit, uint32_t id);
sys_exit_e free_thread(uint32_t id);
void clean_pool(void);
extern void next_thread(void);



#endif
