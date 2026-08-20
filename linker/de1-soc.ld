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

/* mkpimage v0 header: 12 bytes at image offset 0x40 (validation 0x31305341,
 * version, flags, length_u32, zero, 16-bit checksum). Reserved even for the JTAG
 * build so the ONE image self-boots after an mkpimage wrap (make preloader). The
 * boot ROM validates it; the reset branch at 0x00 skips over it. */
_preloader_header_offset = 0x40;
_preloader_header_size   = 0x0C;

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
     * Program image: vectors + code + rodata, PINNED AT THE OCRAM BASE.
     *
     * The Cyclone V boot ROM loads a self-boot image to ORIGIN(OCRAM) and
     * branches to ORIGIN(OCRAM): byte 0 must be the reset vector, and image
     * offset 0x40 must hold the mkpimage v0 header. The JTAG build keeps this
     * exact shape, so the same test.elf self-boots after an mkpimage wrap -- no
     * second linker. JTAG entry is position-independent (reg pc + runtime VBAR),
     * so the base placement costs it nothing.
     *
     * The 0x40 header hole requires the reset/exception code to live in .text,
     * NOT ._vectors -- see the ".text" split in kernel/startup.s. ._vectors is
     * therefore exactly the 8-entry (0x20-byte) branch table.
     *
     * 4 KB-aligned/padded so the MMU L2 table for section 0xFFF marks exactly
     * [_itext_start,_itext_end) Normal-cacheable (I-cache) and everything else in
     * OCRAM (host_shared, data, GIC/timer) Device.
     * --------------------------------------------------------------------- */

    .itext ORIGIN(OCRAM) :
    {
        _itext_start = .;                       /* == ORIGIN(OCRAM): boot-ROM landing */

        KEEP(*(._vectors))                      /* 8-entry branch table, 0x00..0x1F */




        . = _itext_start + _preloader_header_offset;
        _preloader_header = .;
        . = . + _preloader_header_size;

        . = ALIGN(10);

        _boot_entry_addr = .;
        KEEP(*(.boot_entry))

        *(.text*)


        *(.rodata*)

        . = ALIGN(0x1000);
        _itext_end = .;
    } > OCRAM



    /* ---------------------------------------------------------------------
     * Host-shared NOLOAD region (telemetry + fault record).
     *
     * One merged region the host reads over JTAG; its own 4 KB Device (uncached)
     * page so phys reads are coherent. Sits ABOVE the code now -- the code owns
     * the OCRAM base (boot-ROM landing), so this can no longer live there.
     * NOLOAD: the firmware fills it at runtime, so a warm reset/reload survives.
     * --------------------------------------------------------------------- */

    .host_shared ALIGN(0x1000) (NOLOAD) :
    {
        _host_shared_start = .;

        _telemetry_start = .;
        KEEP(*(.telemetry))           /* g_metrics, g_edf_metrics */
        . = ALIGN(8);
        _telemetry_end = .;

        _fault_start = .;
        KEEP(*(.fault))               /* g_fault */

        . = ALIGN(0x1000);
        _host_shared_end = .;
    } > OCRAM

    _telemetry_size = _telemetry_end - _telemetry_start;



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
    /* Self-boot: the reset vector MUST be at the boot-ROM landing (ORIGIN(OCRAM)),
       and the mkpimage header hole MUST be at image offset 0x40. */
    ASSERT(_itext_start == ORIGIN(OCRAM),
        "reset vector table is not at the OCRAM base (boot ROM branches to ORIGIN(OCRAM))")
    ASSERT(_preloader_header == ORIGIN(OCRAM) + 0x40,
        "mkpimage header hole is not at image offset 0x40")

    ASSERT((_host_shared_start & (0x1000 - 1)) == 0,
        "host_shared does not start on a 4096-byte page boundary")
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
        
    ASSERT(_boot_entry_addr == ORIGIN(OCRAM) + 0x50,
       "Cyclone V boot entry is not at OCRAM + 0x50")
}
