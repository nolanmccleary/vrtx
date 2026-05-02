#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

/* Clock Manager: 0xFFD04000 */
#define CM_BASE                 0xFFD04000U
#define CM_R(off)               (*(volatile uint32_t*)(CM_BASE + (off)))

#define CM_CTRL                 CM_R(0x00)
#define CM_BYPASS               CM_R(0x04)
#define CM_INTER                CM_R(0x08)
#define CM_STAT                 CM_R(0x14)

#define CM_MAINPLL_VCO          CM_R(0x40)
#define CM_MAINPLL_MPUCLK       CM_R(0x48)
#define CM_MAINPLL_MAINCLK      CM_R(0x4C)
#define CM_MAINPLL_DBGATCLK     CM_R(0x50)
#define CM_MAINPLL_MAINQSPICLK  CM_R(0x54)
#define CM_MAINPLL_MAINNANDSDMMCCLK CM_R(0x58)
#define CM_MAINPLL_CFGS2FUSER0CLK  CM_R(0x5C)
#define CM_MAINPLL_EN           CM_R(0x60)
#define CM_MAINPLL_MAINDIV      CM_R(0x64)
#define CM_MAINPLL_DBGDIV       CM_R(0x68)
#define CM_MAINPLL_TRACEDIV     CM_R(0x6C)
#define CM_MAINPLL_L4SRC        CM_R(0x70)

#define CM_PERPLL_VCO           CM_R(0x80)
#define CM_PERPLL_EMAC0CLK      CM_R(0x88)
#define CM_PERPLL_EMAC1CLK      CM_R(0x8C)
#define CM_PERPLL_PERQSPICLK    CM_R(0x90)
#define CM_PERPLL_PERNANDSDMMCCLK CM_R(0x94)
#define CM_PERPLL_PERBASECLK    CM_R(0x98)
#define CM_PERPLL_S2FUSER1CLK   CM_R(0x9C)
#define CM_PERPLL_EN            CM_R(0xA0)
#define CM_PERPLL_DIV           CM_R(0xA4)
#define CM_PERPLL_GPIODIV       CM_R(0xA8)
#define CM_PERPLL_SRC           CM_R(0xAC)

#define CM_SDRPLL_VCO           CM_R(0xC0)
#define CM_SDRPLL_DDRDQSCLK     CM_R(0xC8)
#define CM_SDRPLL_DDR2XDQSCLK  CM_R(0xCC)
#define CM_SDRPLL_DDRDQCLK      CM_R(0xD0)
#define CM_SDRPLL_S2FUSER2CLK   CM_R(0xD4)
#define CM_SDRPLL_EN            CM_R(0xD8)

#define CM_ALTR_MPUCLK          CM_R(0xE0)

#define CM_BYPASS_PERPLL        (1U << 3)
#define CM_BYPASS_SDRPLL        (1U << 1)
#define CM_BYPASS_MAINPLL       (1U << 0)

#define CM_INTER_MAINPLLLOST    (1U << 3)
#define CM_INTER_PERPLLLOST     (1U << 4)
#define CM_INTER_SDRPLLLOST     (1U << 5)
#define CM_INTER_MAINPLLLOCKED  (1U << 6)
#define CM_INTER_PERPLLLOCKED   (1U << 7)
#define CM_INTER_SDRPLLLOCKED   (1U << 8)
#define CM_LOCKED_MASK          (CM_INTER_MAINPLLLOCKED | CM_INTER_PERPLLLOCKED | CM_INTER_SDRPLLLOCKED)

#define CM_STAT_BUSY            (1U << 0)
#define CM_CTRL_SAFEMODE        (1U << 0)
#define CM_VCO_EN               (1U << 1)
#define CM_VCO_OUTRESETALL      (1U << 24)
#define CM_VCO_REGEXTSEL        (1U << 31)
#define CM_VCO_RESET_VALUE      0x8001000DU

#define CM_MAINPLL_EN_L4MPCLK       (1U << 2)
#define CM_MAINPLL_EN_DBGATCLK      (1U << 4)
#define CM_MAINPLL_EN_DBGCLK        (1U << 5)
#define CM_MAINPLL_EN_DBGTRACECLK   (1U << 6)
#define CM_MAINPLL_EN_DBGTIMERCLK   (1U << 7)
#define CM_MAINPLL_EN_S2FUSER0CLK   (1U << 9)
#define CM_PERPLL_EN_NANDCLK        (1U << 10)

#define CM_SDRPLL_PHASE_MASK        0x00000E00U

/* SDR Controller: 0xFFC25000 (SOCFPGA_SDR_ADDRESS=0xFFC20000 | 0x5000) */
#define SDR_BASE                0xFFC25000U
#define SDR_R(off)              (*(volatile uint32_t*)(SDR_BASE + (off)))

/* Offsets match struct socfpga_sdr_ctrl from sdram_gen5.h */
#define SDR_CTRLCFG             SDR_R(0x000)
#define SDR_DRAMTIMING1         SDR_R(0x004)
#define SDR_DRAMTIMING2         SDR_R(0x008)
#define SDR_DRAMTIMING3         SDR_R(0x00C)
#define SDR_DRAMTIMING4         SDR_R(0x010)
#define SDR_LOWPWRTIMING        SDR_R(0x014)
#define SDR_DRAMODT             SDR_R(0x018)
#define SDR_EXTRATIME1          SDR_R(0x01C)
/* padding[3] at 0x20, 0x24, 0x28 */
#define SDR_DRAMADDRW           SDR_R(0x02C)
#define SDR_DRAMIFWIDTH         SDR_R(0x030)
#define SDR_DRAMDEVWIDTH        SDR_R(0x034)
/* dram_sts at 0x38 */
#define SDR_DRAMINTR            SDR_R(0x03C)
/* sbe/dbe/err/drop at 0x40-0x50 */
#define SDR_LOWPWREQ            SDR_R(0x054)
/* lowpwr_ack at 0x58 */
#define SDR_STATICCFG           SDR_R(0x05C)
#define SDR_CTRLWIDTH           SDR_R(0x060)
#define SDR_CPORTWIDTH          SDR_R(0x064)
#define SDR_CPORTWMAP           SDR_R(0x068)
#define SDR_CPORTRMAP           SDR_R(0x06C)
#define SDR_RFIFOCMAP           SDR_R(0x070)
#define SDR_WFIFOCMAP           SDR_R(0x074)
#define SDR_CPORTRDWR           SDR_R(0x078)
#define SDR_PORTCFG             SDR_R(0x07C)
/* fpgaport_rst at 0x80, padding1 at 0x84 */
#define SDR_FIFOCFG             SDR_R(0x088)
/* protport_default at 0x8C, prot_rules at 0x90-0x9C */
/* padding2[3] at 0xA0-0xA8 */
#define SDR_MPPRIORITY          SDR_R(0x0AC)
#define SDR_MPWEIGHT0           SDR_R(0x0B0)
#define SDR_MPWEIGHT1           SDR_R(0x0B4)
#define SDR_MPWEIGHT2           SDR_R(0x0B8)
#define SDR_MPWEIGHT3           SDR_R(0x0BC)
#define SDR_MPPACING0           SDR_R(0x0C0)
#define SDR_MPPACING1           SDR_R(0x0C4)
#define SDR_MPPACING2           SDR_R(0x0C8)
#define SDR_MPPACING3           SDR_R(0x0CC)
#define SDR_MPTHRESHOLD0        SDR_R(0x0D0)
#define SDR_MPTHRESHOLD1        SDR_R(0x0D4)
#define SDR_MPTHRESHOLD2        SDR_R(0x0D8)
/* padding3[29] at 0xDC-0x14C */
#define SDR_PHYCTRL0            SDR_R(0x150)

#define SDR_STATICCFG_APPLYCFG  (1U << 3)

/* Scan Manager: 0xFFF02000 */
#define SCANMGR_BASE            0xFFF02000U
#define SCANMGR_STAT            (*(volatile uint32_t*)(SCANMGR_BASE + 0x00))
#define SCANMGR_EN              (*(volatile uint32_t*)(SCANMGR_BASE + 0x04))
/* padding at 0x08, 0x0C */
#define SCANMGR_FIFO_SINGLE     (*(volatile uint32_t*)(SCANMGR_BASE + 0x10))
#define SCANMGR_FIFO_DOUBLE     (*(volatile uint32_t*)(SCANMGR_BASE + 0x14))
/* triple byte at 0x18 */
#define SCANMGR_FIFO_QUAD       (*(volatile uint32_t*)(SCANMGR_BASE + 0x1C))

#define SCANMGR_STAT_ACTIVE     (1U << 31)
#define SCANMGR_STAT_WFIFOCNT   0x70000000U

/* System Manager: 0xFFD08000
 * Freeze controller struct at +0x40:
 *   vioctrl(+0), padding[3](+4,+8,+12), hioctrl(+16), src(+20), hwctrl(+24)
 */
#define FRZCTRL_HIOCTRL         (*(volatile uint32_t*)(0xFFD08000U + 0x40U + 0x10U))
#define FRZCTRL_HIOCTRL_DLLRST  (1U << 5)

void pll_init(void);
void scan_mgr_init(void);
void sdram_ctrl_init(void);

#endif /* BOOT_H */
