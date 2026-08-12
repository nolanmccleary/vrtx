OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset_handler)


MEMORY
{
    SDRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x40000000
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}


/* -------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */

_stack_size     = 0x1000;
_abt_stack_size = 0x200;
_und_stack_size = 0x200;

_bank_size = 0x80;

_heap_offset = 0x200000;
_heap_size   = 0x01000000;

_capture_region = 0x80;


SECTIONS
{

    /* ---------------------------------------------------------------------
     * SDRAM heap
     * --------------------------------------------------------------------- */

    .heap ORIGIN(SDRAM) + _heap_offset (NOLOAD) :
    {
        . = ALIGN(8);

        _heap_start = .;

        . = . + _heap_size;

        _heap_end = .;
    } > SDRAM



    /* ---------------------------------------------------------------------
     * Host/test status region
     * --------------------------------------------------------------------- */

    .status ORIGIN(OCRAM) (NOLOAD) :
    {
        _status_base = .;

        . = . + _capture_region;
    } > OCRAM



    /* ---------------------------------------------------------------------
     * Exception vectors
     * --------------------------------------------------------------------- */

    ._vectors ORIGIN(OCRAM) + _capture_region :
    {
        KEEP(*(._vectors))

        . = ALIGN(4);
    } > OCRAM



    /* ---------------------------------------------------------------------
     * Program image
     * --------------------------------------------------------------------- */

    .text :
    {
        *(.text*)
        *(.rodata*)

        . = ALIGN(4);
    } > OCRAM



    /* ---------------------------------------------------------------------
     * Static ELF metadata
     * --------------------------------------------------------------------- */

    .qmeta_hdr :
    {
        . = ALIGN(4);

        KEEP(*(.qmeta_hdr))
    } > OCRAM


    .qmeta_regions :
    {
        . = ALIGN(4);

        KEEP(*(.qmeta_regions))
    } > OCRAM



    /* ---------------------------------------------------------------------
     * Initialized data
     * --------------------------------------------------------------------- */

    .data :
    {
        _data_start = .;

        *(.data*)

        . = ALIGN(4);

        _data_end = .;
    } > OCRAM

    _data_size = _data_end - _data_start;



    /* ---------------------------------------------------------------------
     * Zero-initialized data
     * --------------------------------------------------------------------- */

    .bss :
    {
        _bss_start = .;

        *(.bss*)
        *(COMMON)

        . = ALIGN(4);

        _bss_end = .;
    } > OCRAM

    _bss_size = _bss_end - _bss_start;



    /* ---------------------------------------------------------------------
     * Benchmark telemetry
     *
     * NOLOAD: telemetry_init() initializes this explicitly.
     * --------------------------------------------------------------------- */

    .telemetry (NOLOAD) :
    {
        . = ALIGN(8);

        _telemetry_start = .;

        KEEP(*(.telemetry))

        . = ALIGN(8);

        _telemetry_end = .;
    } > OCRAM

    _telemetry_size = _telemetry_end - _telemetry_start;



    /* ---------------------------------------------------------------------
     * OCRAM stacks
     *
     * top-down:
     *
     *     UND
     *     ABT
     *     FIQ
     *     IRQ
     *     SVC
     *     SYS
     * --------------------------------------------------------------------- */

    _und_stack_top = ORIGIN(OCRAM) + LENGTH(OCRAM) - 4;

    _abt_stack_top = _und_stack_top - _und_stack_size;

    _fiq_stack_top = _abt_stack_top - _abt_stack_size;

    _irq_stack_top = _fiq_stack_top - _stack_size;

    _svc_stack_top = _irq_stack_top - _stack_size;

    _sys_stack_top = _svc_stack_top - _stack_size;



    ASSERT(
        _telemetry_end <= _sys_stack_top - _stack_size,
        "ERROR: .telemetry overflows OCRAM into the SYS stack"
    )
}
