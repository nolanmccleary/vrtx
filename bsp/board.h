#ifndef BOARD_H
#define BOARD_H

/* -------------------------------------------------------------------------
 * Board selector.
 *
 * A single -DBOARD=<id> chooses exactly one target; the matching block below
 * supplies the hardware-variant facts (peripheral base addresses, and later the
 * memory map / SMP-release protocol / whether SDRAM bringup runs). The
 * scheduler/mutex/heap/thread code is board-agnostic -- NO board conditionals.
 *
 * MODE_TEST is orthogonal: any board builds with or without the test payload.
 *
 * Adding a board: pick the next id and add a block.
 * ------------------------------------------------------------------------- */

#define BOARD_CYCLONE_V 1
#define BOARD_QEMU      2

#ifndef BOARD
#define BOARD BOARD_CYCLONE_V
#endif


#if BOARD == BOARD_CYCLONE_V

/* DE1-SoC / Intel Cyclone V SoC (dual Cortex-A9). A9 MPCore private peripheral
 * region (PERIPHBASE) at 0xFFFEC000: SCU +0x000, GICC +0x100, gtimer +0x200,
 * GICD +0x1000. */
#define BOARD_PERIPHBASE    0xFFFEC000u
#define BOARD_WDT_BASE      0xFFD0200Cu    /* L4 watchdog 0 feed reg (base 0xFFD02000 + 0x0C) */

#elif BOARD == BOARD_QEMU

/* QEMU vexpress-a9 (dual Cortex-A9). MPCore PERIPHBASE at 0x1E000000. */
#define BOARD_PERIPHBASE    0x1E000000u
#define BOARD_WDT_BASE      0x1000000Cu    /* placeholder; QEMU WDT feed is a no-op */

#else
#error "unknown BOARD (expected BOARD_CYCLONE_V or BOARD_QEMU)"
#endif


/* Derived from PERIPHBASE -- fixed offsets in the A9 MPCore private region. */
#define BOARD_GICC_BASE     (BOARD_PERIPHBASE + 0x100u)
#define BOARD_GTIMER_BASE   (BOARD_PERIPHBASE + 0x200u)
#define BOARD_GICD_BASE     (BOARD_PERIPHBASE + 0x1000u)

#endif
