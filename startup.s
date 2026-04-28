.syntax unified
.arm

.equ MODE_USR, 0x10
.equ MODE_FIQ, 0x11
.equ MODE_IRQ, 0x12
.equ MODE_SVC, 0x13
.equ MODE_MON, 0x16
.equ MODE_ABT, 0x17
.equ MODE_HYP, 0x1A
.equ MODE_UND, 0x1B
.equ MODE_SYS, 0x1F
.equ I_BIT, 0x80   @ IRQ disable/mask bit, CPSR[7]
.equ F_BIT, 0x40   @ FIQ disable/mask bit, CPSR[6]



@; CERTIFIED OHIO STEPPER GANG GANG
@; We want caches and branch prediction enabled. We don't want virtual memory enabled


.global _vectors
.global _reset_handler


_vectors:
    B _reset_handler @; rst interrupt vectors here by abi contract
    B _undef_handler
    B _swi_handler
    B _prefetch_handler
    B _data_handler
    NOP               @; Reserved vector
    B _irq_handler
    B _fiq_handler


_reset_handler:
    ldr r0, =_fiq_stack_top

    @; SET STACK POINTERS FOR EACH MODE

    msr CPSR_c, #(MODE_FIQ | I_BIT | F_BIT)
    mov sp, r0
    ldr r1, =_stack_size
    sub r0, r0, r1 

    msr CPSR_c, #(MODE_IRQ | I_BIT | F_BIT)
    mov sp, r0
    ldr r1, =_stack_size
    sub r0, r0, r1 

    msr CPSR_c, #(MODE_SVC | I_BIT | F_BIT)
    mov sp, r0
    ldr r1, =_stack_size
    sub r0, r0, r1 

    msr CPSR_c, #(MODE_SYS | I_BIT | F_BIT)
    mov sp, r0
    ldr r1, =_stack_size
    sub r0, r0, r1 


    @; Disabling MMU and caches explicitly here, note that A9 will should do this automatically upon true reset however we may wish to vector to reset handler upon some other condition in the future

    @; SELECTION HIERARCHY: opcode1->CRn->CRm->opcode2
    @; opcode1: domain select e.g. sys, addr, virt
    @; CRn: bank select e.g. control, fault status
    @; CRm bank: file select e.g. prim control, lockdown
    @; opcode2: reg select e.g. SCTLR, ACTLR, CPACR
    mrc p15, 0, r1, c1, c0, 0 @; decoded: cp15 to r1, domain=sys, crn=control, crm=primctl, regsel=SCTLR (sys ctl)

    @; DISABLE MMU
    bic r1, r1, #0x1 @ clear MMU enable 

    @; DISABLE L1
    bic r1, r1, #(0x1 << 12) @ I-cache disable
    bic r1, r1, #(0x1 << 2)  @ D-cache disable

    DSB @; finish prior activity
    mcr p15, 0, r1, c1, c0, 0 @; decoded: r1 to cp15, domain=sys, crn=control, crm=primctl, regsel=SCTLR (sys ctl)
    ISB @; pipeline flush

    @; INVALIDATE L1 I-CACHE
    mov r1, #0
    
    DSB
    mcr p15, 0, r1, c7, c5, 0
    ISB

    @; WALK D-CACHE (NO INVALIDATE OPERATION LIKE I-CACHE CTLR POSESSES)
    mrc p15, 1, r0, c0, c0, 0 @; 1: cache/TLB maintenance domain, c0: ID bank -> read cache size id
    mov r3, #0x1FF
    and r0, r3, r0, lsr #13 @; r0 = numsets - 1; r0 = (r0 >> 13) & 0x1FF
    mov r1, #0

@; ASSUMES WE ARE USING 4-WAY SET-ASSOCIATIVE CACHE; for way in ways { for set in sets }; may need to adjust mask above depending on how many sets present
way_loop:
    mov r3, #0
set_loop:
    mov r2, r1, lsl #30
    orr r2, r2, r3, lsl #5 
    mcr p15, 0, r2, c7, c6, 2 
    add r3, r3, #1 
    cmp r0, r3
    BGT set_loop

    add r1, r1, #1
    cmp r1, #4 
    BNE way_loop

    @; INVALIDATE TLB
    mcr p15, 0, r1, c8, c7, 0

    @; BRANCH PREDICTION ENABLE
    mov r1, #0
    mrc p15, 0, r1, c1, c0, 0
    orr r1, r1, #(0x1<<11)
    mcr p15, 0, r1, c1, c0, 0


    bl main
    










_undef_handler:
_swi_handler:
_prefetch_handler:
_data_handler:
_irq_handler:
_fiq_handler:
