OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset_handler)


_sdram_length = 0x40000000;   /* 1073741824 bytes (1.07e9) of HPS DDR3 */


MEMORY
{
    SDRAM (rwx) : ORIGIN = 0x00000000, LENGTH = _sdram_length
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}

/* Base address of OCRAM, exported so bsp.c derives its L1 block index from the map
 * instead of hardcoding it. */
_ocram_origin = ORIGIN(OCRAM);


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
     * MMU translation tables: reserved, uninitialized DDR below the heap.
     * bsp.c fills them at boot. The L1 table must be 16 KB-aligned (TTBR0
     * requirement); the L2 table 1 KB-aligned. Sizes are entry_count x 4 bytes.
     * --------------------------------------------------------------------- */

    .mmu_tables ORIGIN(SDRAM) + 0x00100000 (NOLOAD) :
    {
        . = ALIGN(16K);
        _l1_table_base = .;
        . = . + 4096 * 4;      /* 4096 L1 entries x 4 bytes = 16384 bytes */

        . = ALIGN(1K);
        _l2_table_base = .;
        . = . + 256 * 4;       /* 256 L2 entries x 4 bytes = 1024 bytes */
    } > SDRAM



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
     * Host-shared NOLOAD region (status flags + telemetry + fault record)
     *
     * One merged region the host reads over JTAG. NOLOAD: the firmware
     * initializes each part at runtime (telemetry_init, fault handlers), so a
     * warm reset / reload leaves it intact.
     * --------------------------------------------------------------------- */

    .host_shared ORIGIN(OCRAM) (NOLOAD) :
    {
        _status_base = .;
        . = . + _capture_region;      /* status/flag words at OCRAM origin */

        . = ALIGN(8);
        _telemetry_start = .;
        KEEP(*(.telemetry))           /* g_telemetry */
        . = ALIGN(8);
        _telemetry_end = .;

        _fault_start = .;
        KEEP(*(.fault))               /* g_fault */

        . = ALIGN(0x1000);          /* host_shared gets its own special 4kb uncached page */
        _host_shared_end = .;
    } > OCRAM

    _telemetry_size = _telemetry_end - _telemetry_start;



    /* ---------------------------------------------------------------------
     * Program image: vectors + code + rodata.
     *
     * 4 KB-aligned and page-padded so the MMU L2 table for section 0xFFF can mark
     * exactly these pages Normal-cacheable (I-cache) while GIC/timer, host_shared,
     * and the data pages below stay Device. Vectors go first; _itext_start is
     * 4 KB-aligned so VBAR's 32-byte requirement is trivially met.
     * --------------------------------------------------------------------- */

    .itext ALIGN(0x1000) :
    {
        _itext_start = .;

        KEEP(*(._vectors))
        . = ALIGN(4);

        *(.text*)
        *(.rodata*)

        . = ALIGN(0x1000);
        _itext_end = .;
    } > OCRAM



    /* ---------------------------------------------------------------------
     * Initialized data
     * --------------------------------------------------------------------- */

    .data ALIGN(0x1000) :
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
        _bss_end <= _sys_stack_top - _stack_size,
        "ERROR: OCRAM image overflows into the SYS stack"
    )
}
