#ifndef __MIN_HEAP_H_ 
#define __MIN_HEAP_H_


#include "thread.h"


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





heap_op_e insert_node(heap_t* heap, heap_node_t* new);
heap_op_e smart_insert(heap_t* heap, thread_t* thread);
heap_op_e remove_node(heap_t* heap);
heap_op_e pop_heap(heap_node_t* dest, heap_t* heap);




#endif
