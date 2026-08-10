#ifndef __TLSF_H__
#define __TLSF_H__

#include "stddef.h"



typedef enum 
{
    HEAP_OP_OK,
    HEAP_OP_FAIL,
}   heap_op_e;




heap_op_e   heap_init(void);
heap_op_e   heap_destroy(void);
void*       kMalloc(size_t size);
heap_op_e   kFree(void* target);

#endif
