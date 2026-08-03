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



#endif
