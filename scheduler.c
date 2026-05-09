#include <stdbool.h>
#include <stddef.h>
#include "scheduler.h"
#include "flags.h"



static uint32_t num_tasks = 0;


uint32_t gTick = 0;

static task_t tasks[MAX_NUM_TASKS] = {0};



void sched_init(void)
{
    for(size_t i = 0; i < MAX_NUM_TASKS; i++)
    {
        task_t task = {0, 0, 0, false, NULL}; 
        tasks[i] = task;
    }
}


bool add_task(task_t task)
{
    if(num_tasks < MAX_NUM_TASKS)
    {
        for(size_t i = 0; i < MAX_NUM_TASKS; i++)
        {
            if(tasks[i].running == false)
            {
                tasks[i] = task;
                tasks[i].crit = gTick + task.period;
                tasks[i].running = true;
                num_tasks++;
                return true;
            }
        }
    }

    else return false;
}


bool clear_task(uint32_t id)
{
    for(size_t i = 0; i < MAX_NUM_TASKS; i++)
    {
        if(tasks[i].id == id)
        {
            tasks[i].running = false;
            num_tasks--;
            return true;
        }
    }

    return false;
}




void task_handler(void)
{
    for(size_t i = 0; i < MAX_NUM_TASKS; i++)
    {
        if(tasks[i].running && tasks[i].crit <= gTick)
        {
            tasks[i].func();
            tasks[i].crit = gTick + tasks[i].period;
        }
    }
}
