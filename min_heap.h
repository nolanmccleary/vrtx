#ifndef __MIN_HEAP_H_ 
#define __MIN_HEAP_H_




#define MAX_NODES 100


typedef enum
{
    OP_OK,
    OP_FAILED,
}   heap_op_e;






typedef struct 
{
    int order;

    char* payload;
    size_t payload_size;
    
}   heap_node_t;



typedef struct
{
    heap_node_t heap [MAX_NODES];
    int curr_index;
}   heap_t;


#endif
