    .syntax unified
    .arm

    .section .vectors, "ax"
    .global _vectors
_vectors:
    ldr pc, =_reset
    b .
    b .
    b .
    b .
    nop
    b .
    b .

    .text
    .global _reset
_reset:
    cpsid if, #0x13

    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #(1 << 0)
    bic r0, r0, #(1 << 2)
    bic r0, r0, #(1 << 12)
    orr r0, r0, #(1 << 13)
    mcr p15, 0, r0, c1, c0, 0
    isb

    ldr sp, =_svc_stack_top

    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
1:  cmp r0, r1
    strlt r2, [r0], #4
    blt 1b

    bl main
    b .
