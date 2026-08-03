#ifndef __MIN_HEAP_H_ 
#define __MIN_HEAP_H_


#include "preempt_sched.h"


#define MAX_NODES 100


typedef enum
{
    OP_OK,
    OP_FAILED,
}   heap_op_e;



typedef struct 
{
    int order;
    thread_t* thread;
}   heap_node_t;



typedef struct
{
    heap_node_t heap [MAX_NODES];
    int curr_index;
}   heap_t;


#endif
