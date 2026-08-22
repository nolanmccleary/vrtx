OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset_handler)


_sdram_length = 0x40000000;   /* 1073741824 bytes (1.07e9) of HPS DDR3 */


MEMORY
{
    SDRAM (rwx) : ORIGIN = 0x00000000, LENGTH = _sdram_length
    OCRAM (rwx) : ORIGIN = 0xFFFF0000, LENGTH = 64K
}


_ocram_origin = ORIGIN(OCRAM);


/* -------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */

_stack_size     = 0x400;
_abt_stack_size = 0x200;
_und_stack_size = 0x200;

_bank_size = 0x80;

_heap_offset = 0x200000;
_heap_size   = 0x01000000;


_preloader_header_offset = 0x40;
_preloader_header_size   = 0x0C;


_l3_boot_region_end = 0x00100000;   /* 1048576 bytes */


SECTIONS
{

  /* 4096 l1 sections at 1MB each; 256 l2 pages at 2^12 bytes each to cover the unmapped zone 
   * */ 
    
    .mmu_tables ORIGIN(SDRAM) (NOLOAD) :
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




    .itext ORIGIN(OCRAM) :
    {
        _itext_start = .;
        KEEP(*(._vectors))   /* 8-entry branch table expected, 0x00..0x1F */
        . = _itext_start + _preloader_header_offset;
        _preloader_header = .;
        . = . + _preloader_header_size;
        KEEP(*(.boot_entry))


        *(.text*)
        *(.rodata*)

        . = ALIGN(0x1000);
        _itext_end = .;
    } > OCRAM





    .host_shared ALIGN(0x1000) (NOLOAD) :
    {
        _host_shared_start = .;

        KEEP(*(.telemetry))
        . = ALIGN(8);
        KEEP(*(.fault))

        . = ALIGN(0x1000);
        _host_shared_end = .;
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

    /* CPU0 bank: top of OCRAM, growing down. */
    _cpu0_und_stack_top = ORIGIN(OCRAM) + LENGTH(OCRAM) - 4;
    _cpu0_abt_stack_top = _cpu0_und_stack_top - _und_stack_size;
    _cpu0_fiq_stack_top = _cpu0_abt_stack_top - _abt_stack_size;
    _cpu0_irq_stack_top = _cpu0_fiq_stack_top - _stack_size;
    _cpu0_svc_stack_top = _cpu0_irq_stack_top - _stack_size;
    _cpu0_sys_stack_top = _cpu0_svc_stack_top - _stack_size;

    /* CPU1 bank: directly below CPU0's, identical sizes. */
    _cpu1_und_stack_top = _cpu0_sys_stack_top - _stack_size;
    _cpu1_abt_stack_top = _cpu1_und_stack_top - _und_stack_size;
    _cpu1_fiq_stack_top = _cpu1_abt_stack_top - _abt_stack_size;
    _cpu1_irq_stack_top = _cpu1_fiq_stack_top - _stack_size;
    _cpu1_svc_stack_top = _cpu1_irq_stack_top - _stack_size;
    _cpu1_sys_stack_top = _cpu1_svc_stack_top - _stack_size;



    /* =====================================================================
     * Link-time static assertions -- every invariant the memory map must hold,
     * grouped here at the bottom. A violation fails the link with the message.
     * ===================================================================== */

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

    /* The runtime OCRAM footprint (image through .bss) must not collide with the
       stacks, which grow down from the top. Lowest stack is now CPU1's SYS bank. */
    ASSERT(_bss_end <= _cpu1_sys_stack_top - _stack_size,
        "OCRAM image + CPU0/CPU1 stacks exceed the 64K OCRAM")

    /* Boot ROM copies the loaded image (vectors + code + rodata + data) into the
       64 KB OCRAM window; assert it fits that budget. */
    ASSERT(_data_end - ORIGIN(OCRAM) <= 64K,
        "OCRAM loaded image exceeds the 64K boot-ROM budget")

}
