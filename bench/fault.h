#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

/*
 * Software fault capture. On any synchronous CPU exception the asm handlers
 * (bsp/startup.s) grab the faulting PC + SPSR, then call fault_capture(), which
 * reads the A9's CP15 fault registers into g_fault (in the merged host-shared
 * NOLOAD OCRAM region). fault_halt() then stops the watchdog and traps at a
 * host-breakpointed marker so OpenOCD can read g_fault + all live registers
 * without the board watchdog-resetting.
 *
 * The numeric vec ids below are duplicated as literal immediates in the asm
 * handlers (bsp/startup.s) -- keep them in sync.
 */

typedef enum
{
    FAULT_NONE     = 0,
    FAULT_UNDEF    = 1,
    FAULT_SWI      = 2,
    FAULT_PREFETCH = 3,
    FAULT_DATA     = 4,
    FAULT_FIQ      = 5
}   fault_vec_e;

#define FAULT_MAGIC 0x464C5431u   /* "FLT1" -- written LAST so the host never reads a partial record */

typedef struct
{
    uint32_t magic;   /* FAULT_MAGIC once fully populated */
    uint32_t vec;     /* fault_vec_e */
    uint32_t pc;      /* faulting instruction address (LR adjusted per-exception) */
    uint32_t spsr;    /* CPSR at the time of the fault (mode/state of faulting code) */
    uint32_t dfsr;    /* data fault status  (valid for FAULT_DATA) */
    uint32_t dfar;    /* data fault address (valid for FAULT_DATA) */
    uint32_t ifsr;    /* instruction fault status  (valid for FAULT_PREFETCH) */
    uint32_t ifar;    /* instruction fault address (valid for FAULT_PREFETCH) */
}   fault_record_t;

extern fault_record_t g_fault;

void fault_capture(uint32_t pc, uint32_t spsr, uint32_t vec);  /* fill g_fault from CP15 */
void fault_trap(void);                                          /* host breakpoint marker */
void fault_halt(void);                                          /* stop WDT, trap, spin */

#endif
