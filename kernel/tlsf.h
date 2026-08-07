#ifndef __TLSF_H__
#define __TLSF_H__




extern char _heap_start;
extern char _heap_end;


#define ALIGN4(A) ((A + 0x3) & ~(0b11)) //round up to nearest word


#define HEAP_START (ALIGN4(((uintptr_t)&_heap_start)))
#define HEAP_END   ((uintptr_t)&_heap_end) & ~0b11
#define HEAP_SIZE  (HEAP_END - HEAP_START)





#endif
