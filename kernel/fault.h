#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>
#include "cpu.h"

typedef enum
{
    FAULT_NONE     = 0,
    FAULT_UNDEF    = 1,
    FAULT_SWI      = 2,
    FAULT_PREFETCH = 3,
    FAULT_DATA     = 4,
    FAULT_FIQ      = 5
}   fault_vec_e;

#define FAULT_MAGIC 0x464C5431u

typedef struct
{
    uint32_t magic;
    uint32_t vec;  
    uint32_t pc;   
    uint32_t spsr; 
    uint32_t dfsr; 
    uint32_t dfar; 
    uint32_t ifsr; 
    uint32_t ifar; 
}   fault_record_t;

extern fault_record_t g_fault[NUM_CPUS];

void fault_capture(uint32_t pc, uint32_t spsr, uint32_t vec); 
void fault_trap(void);                                        
void fault_halt(void);                                        

#endif
