#include <stddef.h>
#include "allocator.h"



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

void heap_init(void)
{
    _head = (heap_entry*)HEAP_START;
    _head->next = NULL;
    _head->is_free = true;
    _head->size = HEAP_SIZE - sizeof(heap_entry);
    heap_initialized = true;
}


void heap_deinit(void)
{
    heap_initialized = false; 
}


void* kMalloc(uint32_t size)
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


void kFree(void* target)
{
    if(target == NULL) return;

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
}
