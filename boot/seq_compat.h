/* Bare-metal compatibility shim for ported sequencer.c */
#ifndef SEQ_COMPAT_H
#define SEQ_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* u-boot integer types */
typedef uint32_t    u32;
typedef uint16_t    u16;
typedef uint8_t     u8;
typedef int         s32;    /* int = 32 bits on ARM; avoid int32_t/int* mismatch */
typedef int16_t     s16;

/* Register access */
#define writel(v, a)    (*(volatile u32 *)(uintptr_t)(a) = (u32)(v))
#define readl(a)        (*(volatile u32 *)(uintptr_t)(a))

#define clrsetbits_le32(a, clr, set) \
    do { u32 _v = readl(a); _v &= ~(u32)(clr); _v |= (u32)(set); writel(_v, a); } while(0)

#define clrbits_le32(a, clr)    clrsetbits_le32(a, clr, 0)
#define setbits_le32(a, set)    clrsetbits_le32(a, 0, set)

/* Logging: strip all debug output */
#define debug(fmt, ...)             ((void)0)
#define debug_cond(cond, fmt, ...)  ((void)0)
#define pr_err(fmt, ...)            ((void)0)
#define pr_warn(fmt, ...)           ((void)0)
#define pr_debug(fmt, ...)          ((void)0)
#define printf(fmt, ...)            ((void)0)

/* hang: spin forever (watchdog will reset if running) */
static inline void hang(void) { for(;;) {} }

/* errno stubs */
#define ETIMEDOUT   110
#define ENODEV      19
#define EINVAL      22
#define EIO         5

/* integer math helpers */
#define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))
#define rounddown(x, y)     ((x) - ((x) % (y)))

/* Cyclone V SDR base address */
#define SOCFPGA_SDR_ADDRESS         0xFFC20000U

/* CTRLCFG field constants needed by dram_is_ddr() */
#define SDR_CTRLGRP_CTRLCFG_MEMTYPE_LSB    0
#define SDR_CTRLGRP_CTRLCFG_MEMTYPE_MASK   0x7U

/* SDRAM config structures (from sdram_gen5.h) */
struct socfpga_sdram_config {
    u32 ctrl_cfg;
    u32 dram_timing1;
    u32 dram_timing2;
    u32 dram_timing3;
    u32 dram_timing4;
    u32 lowpwr_timing;
    u32 dram_odt;
    u32 extratime1;
    u32 dram_addrw;
    u32 dram_if_width;
    u32 dram_dev_width;
    u32 dram_intr;
    u32 lowpwr_eq;
    u32 static_cfg;
    u32 ctrl_width;
    u32 cport_width;
    u32 cport_wmap;
    u32 cport_rmap;
    u32 rfifo_cmap;
    u32 wfifo_cmap;
    u32 cport_rdwr;
    u32 port_cfg;
    u32 fpgaport_rst;
    u32 fifo_cfg;
    u32 mp_priority;
    u32 mp_weight0;
    u32 mp_weight1;
    u32 mp_weight2;
    u32 mp_weight3;
    u32 mp_pacing0;
    u32 mp_pacing1;
    u32 mp_pacing2;
    u32 mp_pacing3;
    u32 mp_threshold0;
    u32 mp_threshold1;
    u32 mp_threshold2;
    u32 phy_ctrl0;
};

struct socfpga_sdram_rw_mgr_config {
    u8  activate_0_and_1;
    u8  activate_0_and_1_wait1;
    u8  activate_0_and_1_wait2;
    u8  activate_1;
    u8  clear_dqs_enable;
    u8  guaranteed_read;
    u8  guaranteed_read_cont;
    u8  guaranteed_write;
    u8  guaranteed_write_wait0;
    u8  guaranteed_write_wait1;
    u8  guaranteed_write_wait2;
    u8  guaranteed_write_wait3;
    u8  idle_loop1;
    u8  idle_loop2;
    /* DDR3 only */
    u8  activate_1_2;
    u8  idle;
    u8  init_reset_0_cke_0;
    u8  init_reset_1_cke_0;
    union { u8 mrs0_dll_reset;      u8 mr_dll_reset; };
    union { u8 mrs0_dll_reset_mirr; u8 mr_dll_reset_mirr; };
    union { u8 mrs0_user;           u8 mr_user; };
    union { u8 mrs0_user_mirr;      u8 mr_user_mirr; };
    union { u8 mrs1;                u8 emr; };
    u8  mrs1_mirr;
    union { u8 mrs2;                u8 emr2; };
    u8  mrs2_mirr;
    union { u8 mrs3;                u8 emr3; };
    u8  mrs3_mirr;
    union { u8 refresh_all;         u8 refresh; };
    u8  rreturn;
    u8  sgle_read;
    union { u8 zqcl;                u8 nop; };
    u8  mr_calib;
    /* shared */
    u8  lfsr_wr_rd_bank_0;
    u8  lfsr_wr_rd_bank_0_data;
    u8  lfsr_wr_rd_bank_0_dqs;
    u8  lfsr_wr_rd_bank_0_nop;
    u8  lfsr_wr_rd_bank_0_wait;
    u8  lfsr_wr_rd_bank_0_wl_1;
    u8  lfsr_wr_rd_dm_bank_0;
    u8  lfsr_wr_rd_dm_bank_0_data;
    u8  lfsr_wr_rd_dm_bank_0_dqs;
    u8  lfsr_wr_rd_dm_bank_0_nop;
    u8  lfsr_wr_rd_dm_bank_0_wait;
    u8  lfsr_wr_rd_dm_bank_0_wl_1;
    u8  precharge_all;
    u8  read_b2b;
    u8  read_b2b_wait1;
    u8  read_b2b_wait2;
    u8  true_mem_data_mask_width;
    u8  mem_address_mirroring;
    u8  mem_data_mask_width;
    u8  mem_data_width;
    u8  mem_dq_per_read_dqs;
    u8  mem_dq_per_write_dqs;
    u8  mem_if_read_dqs_width;
    u8  mem_if_write_dqs_width;
    u8  mem_number_of_cs_per_dimm;
    u8  mem_number_of_ranks;
    u8  mem_virtual_groups_per_read_dqs;
    u8  mem_virtual_groups_per_write_dqs;
};

struct socfpga_sdram_io_config {
    u16 delay_per_opa_tap;
    u8  delay_per_dchain_tap;
    u8  delay_per_dqs_en_dchain_tap;
    u8  dll_chain_length;
    u8  dqdqs_out_phase_max;
    u8  dqs_en_delay_max;
    u8  dqs_en_delay_offset;
    u8  dqs_en_phase_max;
    u8  dqs_in_delay_max;
    u8  dqs_in_reserve;
    u8  dqs_out_reserve;
    u8  io_in_delay_max;
    u8  io_out1_delay_max;
    u8  io_out2_delay_max;
    u8  shift_dqs_en_when_shift_dqs;
};

struct socfpga_sdram_misc_config {
    u32 reg_file_init_seq_signature;
    u16 afi_clk_freq;
    u8  afi_rate_ratio;
    u8  calib_lfifo_offset;
    u8  calib_vfifo_offset;
    u8  enable_super_quick_calibration;
    u8  max_latency_count_width;
    u8  read_valid_fifo_size;
    u8  tinit_cntr0_val;
    u8  tinit_cntr1_val;
    u8  tinit_cntr2_val;
    u8  treset_cntr0_val;
    u8  treset_cntr1_val;
    u8  treset_cntr2_val;
};

const struct socfpga_sdram_config     *socfpga_get_sdram_config(void);
const struct socfpga_sdram_rw_mgr_config *socfpga_get_sdram_rwmgr_config(void);
const struct socfpga_sdram_io_config  *socfpga_get_sdram_io_config(void);
const struct socfpga_sdram_misc_config *socfpga_get_sdram_misc_config(void);
void socfpga_get_seq_ac_init(const u32 **init, unsigned int *nelem);
void socfpga_get_seq_inst_init(const u32 **init, unsigned int *nelem);

/* CTRLCFG DQSTRKEN field */
#define SDR_CTRLGRP_CTRLCFG_DQSTRKEN_MASK          0x00400000U

/* PHY control SET macros (sequencer.c line ~3814) */
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_ACDELAYEN_SET(x)         (((x) << 0)  & 0x00000003U)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_DQDELAYEN_SET(x)         (((x) << 2)  & 0x0000000CU)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_DQSDELAYEN_SET(x)        (((x) << 4)  & 0x00000030U)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_DQSLOGICDELAYEN_SET(x)   (((x) << 6)  & 0x000000C0U)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_RESETDELAYEN_SET(x)      (((x) << 8)  & 0x00000100U)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_LPDDRDIS_SET(x)          (((x) << 9)  & 0x00000200U)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_ADDLATSEL_SET(x)         (((x) << 10) & 0x00000C00U)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_SAMPLECOUNT_19_0_SET(x)  (((x) << 12) & 0xFFFFF000U)

#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_1_LONGIDLESAMPLECOUNT_19_0_SET(x) (((x) << 12) & 0xFFFFF000U)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_1_SAMPLECOUNT_31_20_SET(x)        (((x) << 0)  & 0x00000FFFU)
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_2_LONGIDLESAMPLECOUNT_31_20_SET(x)(((x) << 0)  & 0x00000FFFU)

/* Bit-field widths: SAMPLECOUNT[19:0] = 20 bits, LONGIDLESAMPLECOUNT[19:0] = 20 bits */
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_0_SAMPLECOUNT_19_0_WIDTH           20
#define SDR_CTRLGRP_PHYCTRL_PHYCTRL_1_LONGIDLESAMPLECOUNT_19_0_WIDTH   20

/* SDR controller address (used by sequencer for sdr_ctrl pointer) */
#define SDR_CTRLGRP_ADDRESS         (SOCFPGA_SDR_ADDRESS | 0x5000U)

/* Full sdr_ctrl struct — layout must match hardware exactly. */
struct socfpga_sdr_ctrl {
    u32 ctrl_cfg;           /* 0x000 */
    u32 dram_timing1;       /* 0x004 */
    u32 dram_timing2;       /* 0x008 */
    u32 dram_timing3;       /* 0x00C */
    u32 dram_timing4;       /* 0x010 */
    u32 lowpwr_timing;      /* 0x014 */
    u32 dram_odt;           /* 0x018 */
    u32 extratime1;         /* 0x01C */
    u32 __padding0[3];      /* 0x020–0x028 */
    u32 dram_addrw;         /* 0x02C */
    u32 dram_if_width;      /* 0x030 */
    u32 dram_dev_width;     /* 0x034 */
    u32 dram_sts;           /* 0x038 */
    u32 dram_intr;          /* 0x03C */
    u32 sbe_count;          /* 0x040 */
    u32 dbe_count;          /* 0x044 */
    u32 err_addr;           /* 0x048 */
    u32 drop_count;         /* 0x04C */
    u32 drop_addr;          /* 0x050 */
    u32 lowpwr_eq;          /* 0x054 */
    u32 lowpwr_ack;         /* 0x058 */
    u32 static_cfg;         /* 0x05C */
    u32 ctrl_width;         /* 0x060 */
    u32 cport_width;        /* 0x064 */
    u32 cport_wmap;         /* 0x068 */
    u32 cport_rmap;         /* 0x06C */
    u32 rfifo_cmap;         /* 0x070 */
    u32 wfifo_cmap;         /* 0x074 */
    u32 cport_rdwr;         /* 0x078 */
    u32 port_cfg;           /* 0x07C */
    u32 fpgaport_rst;       /* 0x080 */
    u32 __padding1;         /* 0x084 */
    u32 fifo_cfg;           /* 0x088 */
    u32 protport_default;   /* 0x08C */
    u32 prot_rule_addr;     /* 0x090 */
    u32 prot_rule_id;       /* 0x094 */
    u32 prot_rule_data;     /* 0x098 */
    u32 prot_rule_rdwr;     /* 0x09C */
    u32 __padding2[3];      /* 0x0A0–0x0A8 */
    u32 mp_priority;        /* 0x0AC */
    u32 mp_weight0;         /* 0x0B0 */
    u32 mp_weight1;         /* 0x0B4 */
    u32 mp_weight2;         /* 0x0B8 */
    u32 mp_weight3;         /* 0x0BC */
    u32 mp_pacing0;         /* 0x0C0 */
    u32 mp_pacing1;         /* 0x0C4 */
    u32 mp_pacing2;         /* 0x0C8 */
    u32 mp_pacing3;         /* 0x0CC */
    u32 mp_threshold0;      /* 0x0D0 */
    u32 mp_threshold1;      /* 0x0D4 */
    u32 mp_threshold2;      /* 0x0D8 */
    u32 __padding3[29];     /* 0x0DC–0x14C */
    u32 phy_ctrl0;          /* 0x150 */
    u32 phy_ctrl1;          /* 0x154 */
    u32 phy_ctrl2;          /* 0x158 */
};

#endif /* SEQ_COMPAT_H */
