OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset_handler)



MEMORY {
    SDRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x40000000
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

_stack_size     = 0x1000;   /* 4KB for FIQ/IRQ/SVC/SYS (was 8KB; trimmed to free OCRAM
                               for the telemetry region — deepest use is percolate
                               recursion at depth ~log2(MAX_NODES) plus IRQ frames) */
_abt_stack_size = 0x200;    /* 512 bytes — handler only needs a few words */
_und_stack_size = 0x200;
_bank_size = 0x80; /*128 tasks */

_heap_offset = 0x200000; /* 1MB offset */
_heap_size = 0x01000000; /* 16MB heap */
_capture_region = 0x80;

SECTIONS {
    .pc_bank ORIGIN(SDRAM) (NOLOAD) : {
        
    }

    .task_bank ORIGIN(SDRAM) (NOLOAD) : { /* Max 128 tasks */
        . = ALIGN(8);
        _bank_start = .;
        . = . + _heap_offset;
        _bank_end = .;
    }

    .heap _bank_end (NOLOAD) : {
        . = ALIGN(8);
        _heap_start = .;
        . = . + _heap_size;
        _heap_end = .;
    } > SDRAM


    .status ORIGIN(OCRAM) (NOLOAD) : {
        _status_base = .;
        . = . + _capture_region;
    } > OCRAM

    ._vectors ORIGIN(OCRAM) + _capture_region : {
        KEEP(*(._vectors))
        . = ALIGN(4);
    } > OCRAM

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > OCRAM

    /* Self-describing layout metadata (bench/qmeta.*). LOADED so the host reads it
       straight out of the .elf; contents are all link-time-constant addresses. */
    .qmeta_hdr : {
        . = ALIGN(4);
        KEEP(*(.qmeta_hdr))
    } > OCRAM
    .qmeta_regions : {
        . = ALIGN(4);
        KEEP(*(.qmeta_regions))
    } > OCRAM
    .qmeta_fields : {
        . = ALIGN(4);
        KEEP(*(.qmeta_fields))
    } > OCRAM

    .data : {
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > OCRAM
    _data_size = _data_end - _data_start;

    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > OCRAM
    _bss_size = _bss_end - _bss_start;

    /* Benchmark telemetry — NOLOAD results region, zeroed at runtime by
       telemetry_init(). Only present when a bench build pulls in telemetry.c. */
    .telemetry (NOLOAD) : {
        . = ALIGN(8);
        _telemetry_start = .;
        KEEP(*(.telemetry))
        . = ALIGN(8);
        _telemetry_end = .;
    } > OCRAM
    _telemetry_size = _telemetry_end - _telemetry_start;

    /* OCRAM stacks — top-down: UND, ABT, FIQ, IRQ, SVC, SYS */
    _und_stack_top = ORIGIN(OCRAM) + LENGTH(OCRAM) - 4;
    _abt_stack_top = _und_stack_top - _und_stack_size;
    _fiq_stack_top = _abt_stack_top - _abt_stack_size;
    _irq_stack_top = _fiq_stack_top - _stack_size;
    _svc_stack_top = _irq_stack_top - _stack_size;
    _sys_stack_top = _svc_stack_top - _stack_size;

    /* Ensure the telemetry region does not grow into the lowest (SYS) stack. */
    ASSERT(_telemetry_end <= _sys_stack_top - _stack_size,
           "ERROR: .telemetry overflows OCRAM into the SYS stack")
}
