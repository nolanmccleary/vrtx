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

/* L3 remap "boot region": the MPU decodes physical [0, 0x100000) through the L3
 * interconnect, and NIC301_REMAP can swap what lives there (boot ROM / OCRAM / SDRAM).
 * SDRAM only starts at 0x100000 for the MPU. Anything the hardware reads directly and
 * that must survive a remap toggle -- in particular the MMU translation tables, walked
 * by the page-table walker -- MUST sit at or above this boundary, or it vanishes/changes
 * out from under us when the remap flips. See bsp.c NIC301_REMAP. */
_l3_boot_region_end = 0x00100000;   /* 1048576 bytes */


SECTIONS
{

    /* ---------------------------------------------------------------------
     * MMU translation tables: reserved, uninitialized DDR below the heap.
     * bsp.c fills them at boot. The L1 table must be 16 KB-aligned (TTBR0
     * requirement); the L2 table 1 KB-aligned. Sizes are entry_count x 4 bytes.
     *
     * Placed at 0x100000, exactly at _l3_boot_region_end -- the first SDRAM address
     * the MPU sees, just above the remappable boot region. The walker reads these
     * tables directly, so they must NOT be in [0, 0x100000). The ASSERT below enforces
     * it; do not move .mmu_tables lower.
     * --------------------------------------------------------------------- */

    .mmu_tables ORIGIN(SDRAM) + 0x00100000 (NOLOAD) :
    {
        . = ALIGN(16K);
        _l1_table_base = .;
        . = . + 4096 * 4;      /* 4096 L1 entries x 4 bytes = 16384 bytes */

        . = ALIGN(1K);
        _l2_table_base = .;
        . = . + 256 * 4;       /* 256 L2 entries x 4 bytes = 1024 bytes */

        _mmu_tables_end = .;
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



    /* =====================================================================
     * Link-time static assertions -- every invariant the memory map must hold,
     * grouped here at the bottom. A violation fails the link with the message.
     * ===================================================================== */

    /* MMU tables sit above the L3 remappable boot region [0, 0x100000): the page-table
       walker reads them directly and must never read memory the remap can swap. */
    ASSERT(_l1_table_base >= _l3_boot_region_end,
        "MMU tables fall inside the L3 remappable boot region [0, 0x100000)")

    /* Table alignments the hardware demands: TTBR0 (TTBCR.N=0) needs the L1 table
       16384-byte aligned; an L1 pointer descriptor holds the L2 base in bits [31:10],
       so the L2 table must be 1024-byte aligned. */
    ASSERT((_l1_table_base & (0x4000 - 1)) == 0,
        "L1 table is not 16384-byte aligned (TTBR0 requirement)")
    ASSERT((_l2_table_base & (0x400 - 1)) == 0,
        "L2 table is not 1024-byte aligned (L1 pointer descriptor requirement)")

    /* MMU tables must not run into the heap (kMalloc would hand out table memory), and
       the heap must fit inside the DDR. */
    ASSERT(_mmu_tables_end <= _heap_start,
        "MMU tables overlap the heap")
    ASSERT(_heap_end <= ORIGIN(SDRAM) + _sdram_length,
        "heap extends past the end of DDR")

    /* The MMU L2 split maps [_itext_start, _itext_end) cacheable and every other OCRAM
       sub-block device, at 4096-byte granularity. Each boundary must be 4096-byte aligned
       or one page straddles cacheable code and device data -> uncached code, or cached
       (JTAG-incoherent) data. */
    ASSERT((_host_shared_end & (0x1000 - 1)) == 0,
        "host_shared does not end on a 4096-byte page boundary")
    ASSERT((_itext_start & (0x1000 - 1)) == 0,
        "itext (code) does not start on a 4096-byte page boundary")
    ASSERT((_itext_end & (0x1000 - 1)) == 0,
        "itext (code) does not end on a 4096-byte page boundary")
    ASSERT((_data_start & (0x1000 - 1)) == 0,
        "data does not start on a 4096-byte page boundary")

    /* The OCRAM image (through .bss) must not overflow into the SYS stack. */
    ASSERT(_bss_end <= _sys_stack_top - _stack_size,
        "OCRAM image overflows into the SYS stack")
}
