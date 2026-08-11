#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "tlsf.h"


extern char _heap_start;
extern char _heap_end;


#define ALIGN4(A) ((A + 0x3) & ~(0b11)) //round up to nearest word


#define HEAP_START (ALIGN4(((uintptr_t)&_heap_start)))
#define HEAP_END   ((uintptr_t)&_heap_end) & ~0b11
#define HEAP_SIZE  (HEAP_END - HEAP_START)

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}



typedef struct heap_entry
{
    struct heap_entry* next;
    bool is_free;
    uint32_t size;
}   heap_entry;



static heap_entry* _head = NULL;
static bool heap_initialized = false;

allocator_op_e heap_init(void)
{
    _head = (heap_entry*)HEAP_START;
    _head->next = NULL;
    _head->is_free = true;
    _head->size = HEAP_SIZE - sizeof(heap_entry);
    heap_initialized = true;

    return ALLOC_OP_OK;
}


allocator_op_e heap_deinit(void)
{
    heap_initialized = false; 
    return ALLOC_OP_OK;
}


void* kMalloc(size_t size)
{
    if(!heap_initialized) heap_init();

    heap_entry* node = _head;
    while(node != NULL)
    {
        if(node->size >= ALIGN4(size) && node->is_free)
        {
           if(node->size >= ALIGN4(size) + sizeof(heap_entry) + 4U)
            {
                heap_entry* new = (heap_entry*)((void*)((char*)node + sizeof(heap_entry) + ALIGN4(size)));
                new->next = node->next;
                node->next = new;
                new->size = node->size - ALIGN4(size) - sizeof(heap_entry);
                new->is_free = true;
                node->size = ALIGN4(size);
            }

            node->is_free = false;
            return (void*)((char*)node + sizeof(heap_entry));
        }

        node = node->next;
    }

    return NULL;
}


allocator_op_e kFree(void* target)
{
    if(target == NULL) return ALLOC_OP_FAIL;

    heap_entry* node = (heap_entry*)((char*)target - sizeof(heap_entry));    
    node->is_free = true;

    node = _head;
    while(node != NULL && node->next != NULL)
    {
        if(node->is_free && node->next->is_free == true)
        {
            node->size += node->next->size + sizeof(heap_entry);
            node->next = node->next->next;
        }
        else node = node->next;
    }

    return ALLOC_OP_OK;
}
