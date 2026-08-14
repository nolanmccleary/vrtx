#ifndef __THREAD_H__
#define __THREAD_H__


#include <stdbool.h>
#include <stdint.h>
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
    APERIODIC,
    PERIODIC,
}   thread_periodicity_e;




typedef struct __attribute__((aligned(8))) //Stack should start 8-aligned
{
    char stack[THREAD_STACK_SIZE];
    char* sp;

    thread_periodicity_e periodicity;
    uint32_t period;

    uint32_t release_time;
    uint32_t deadline;

    bool dirty;
    thread_status_e thread_status;

    sys_exit_e (*func)();
}   thread_t;



#endif
