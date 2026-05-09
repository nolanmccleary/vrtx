#ifndef __SCHEDULER_H_
#define __SCHEDULER_H_

#include "stdint.h"


#define MAX_NUM_TASKS 10


typedef struct 
{
    uint32_t period;
    uint32_t crit;
    uint32_t id;
    bool running;
    void (*func)(void);
}   task_t;




extern uint32_t gTick;


void sched_init(void);
bool add_task(task_t task);
bool clear_task(uint32_t id);
void task_handler(void);





#endif
