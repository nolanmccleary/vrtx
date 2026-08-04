#ifndef __THREAD_H__
#define __THREAD_H__



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
    PERIODIC,
    APERIODIC,
}   thread_periodicity_e;




typedef struct
{
    thread_periodicity_e periodicity;
    uint32_t period;

    uint32_t release_time;
    uint32_t deadline;
    thread_status_e thread_status;

    char stack[THREAD_STACK_SIZE];
    char* sp;
    sys_exit_e (*func)(thread_status_e* status);
}   thread_t;



#endif
