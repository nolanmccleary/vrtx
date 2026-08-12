#include <stdint.h>
#include "seq_compat.h"
#include "boot.h"
#include "pll_config.h"
#include "sdram_config.h"
#include "iocsr_config.h"

/* ---- helpers ---- */

static void cm_wait_fsm(void)
{
    while (CM_STAT & CM_STAT_BUSY)
        ;
}

static void cm_write_bypass(uint32_t val)
{
    CM_BYPASS = val;
    cm_wait_fsm();
}

static void cm_write_ctrl(uint32_t val)
{
    CM_CTRL = val;
    cm_wait_fsm();
}

static void cm_wait_lock(void)
{
    while ((CM_INTER & CM_LOCKED_MASK) != CM_LOCKED_MASK)
        ;
}

/* Poll until phase bit clears, write val, poll again. */
static void cm_write_phase(uint32_t val, volatile uint32_t *reg, uint32_t phase_mask)
{
    while (*reg & phase_mask)
        ;
    *reg = val;
    while (*reg & phase_mask)
        ;
}

static void spin(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++)
        ;
}

/* ---- PLL init ---- */

/* Derived from cm_basic_init() in clock_manager_gen5.c.
 * All values hardcoded from board/terasic/de1-soc/qts/pll_config.h.
 *
 * Main PLL:  NUMER=63, DENOM=0  → 1600 MHz
 * Peri PLL:  NUMER=39, DENOM=0  → 1000 MHz
 * SDR  PLL:  NUMER=31, DENOM=0  →  800 MHz
 */
void pll_init(void)
{
    /* VCO base values (BGPWRDN=0, EN=0, REGEXTSEL=0) */
    const uint32_t main_vco  = (uint32_t)(CFG_HPS_MAINPLLGRP_VCO_NUMER << 3)
                             | (uint32_t)(CFG_HPS_MAINPLLGRP_VCO_DENOM << 16);
    const uint32_t peri_vco  = (uint32_t)(CFG_HPS_PERPLLGRP_VCO_NUMER << 3)
                             | (uint32_t)(CFG_HPS_PERPLLGRP_VCO_DENOM << 16)
                             | (uint32_t)(CFG_HPS_PERPLLGRP_VCO_PSRC  << 22);
    const uint32_t sdr_vco   = (uint32_t)(CFG_HPS_SDRPLLGRP_VCO_NUMER << 3)
                             | (uint32_t)(CFG_HPS_SDRPLLGRP_VCO_DENOM << 16)
                             | (uint32_t)(CFG_HPS_SDRPLLGRP_VCO_SSRC  << 22);

    /* SDRAM phase clock values: CNT in bits[8:0], PHASE in bits[11:9] */
    const uint32_t ddrdqsclk    = (CFG_HPS_SDRPLLGRP_DDRDQSCLK_CNT    & 0x1FFU)
                                | (CFG_HPS_SDRPLLGRP_DDRDQSCLK_PHASE   << 9);
    const uint32_t ddr2xdqsclk  = (CFG_HPS_SDRPLLGRP_DDR2XDQSCLK_CNT  & 0x1FFU)
                                | (CFG_HPS_SDRPLLGRP_DDR2XDQSCLK_PHASE << 9);
    const uint32_t ddrdqclk     = (CFG_HPS_SDRPLLGRP_DDRDQCLK_CNT     & 0x1FFU)
                                | (CFG_HPS_SDRPLLGRP_DDRDQCLK_PHASE    << 9);
    const uint32_t s2fuser2clk  = (CFG_HPS_SDRPLLGRP_S2FUSER2CLK_CNT  & 0x1FFU)
                                | (CFG_HPS_SDRPLLGRP_S2FUSER2CLK_PHASE << 9);

    /* maindiv: L3MP[1:0]=1, L3SP[3:2]=1, L4MP[6:4]=1, L4SP[9:7]=1 */
    const uint32_t maindiv =
        ((uint32_t)CFG_HPS_MAINPLLGRP_MAINDIV_L3MPCLK << 0) |
        ((uint32_t)CFG_HPS_MAINPLLGRP_MAINDIV_L3SPCLK << 2) |
        ((uint32_t)CFG_HPS_MAINPLLGRP_MAINDIV_L4MPCLK << 4) |
        ((uint32_t)CFG_HPS_MAINPLLGRP_MAINDIV_L4SPCLK << 7);

    /* dbgdiv: DBGATCLK[1:0]=0, DBGCLK[3:2]=1 */
    const uint32_t dbgdiv =
        ((uint32_t)CFG_HPS_MAINPLLGRP_DBGDIV_DBGATCLK << 0) |
        ((uint32_t)CFG_HPS_MAINPLLGRP_DBGDIV_DBGCLK   << 2);

    /* tracediv: TRACECLK[2:0]=0 */
    const uint32_t tracediv = (uint32_t)CFG_HPS_MAINPLLGRP_TRACEDIV_TRACECLK;

    /* perdiv: USB[2:0], SPIM[5:3], CAN0[8:6], CAN1[11:9] */
    const uint32_t perdiv =
        ((uint32_t)CFG_HPS_PERPLLGRP_DIV_USBCLK  << 0) |
        ((uint32_t)CFG_HPS_PERPLLGRP_DIV_SPIMCLK << 3) |
        ((uint32_t)CFG_HPS_PERPLLGRP_DIV_CAN0CLK << 6) |
        ((uint32_t)CFG_HPS_PERPLLGRP_DIV_CAN1CLK << 9);

    /* persrc: SDMMC[1:0], NAND[3:2], QSPI[5:4] */
    const uint32_t persrc =
        ((uint32_t)CFG_HPS_PERPLLGRP_SRC_SDMMC << 0) |
        ((uint32_t)CFG_HPS_PERPLLGRP_SRC_NAND  << 2) |
        ((uint32_t)CFG_HPS_PERPLLGRP_SRC_QSPI  << 4);

    /* l4src: L4MP[0]=1, L4SP[1]=1 */
    const uint32_t l4src =
        ((uint32_t)CFG_HPS_MAINPLLGRP_L4SRC_L4MP << 0) |
        ((uint32_t)CFG_HPS_MAINPLLGRP_L4SRC_L4SP << 1);

    /* 1. Gate all software-managed clocks. */
    CM_PERPLL_EN &= ~CM_PERPLL_EN_NANDCLK;
    /* keep debug/bridge clocks; gate everything else */
    CM_MAINPLL_EN = CM_MAINPLL_EN_DBGTIMERCLK | CM_MAINPLL_EN_DBGTRACECLK
                  | CM_MAINPLL_EN_DBGCLK | CM_MAINPLL_EN_DBGATCLK
                  | CM_MAINPLL_EN_S2FUSER0CLK | CM_MAINPLL_EN_L4MPCLK;
    CM_SDRPLL_EN  = 0;
    CM_PERPLL_EN  = 0;

    /* 2. Bypass all PLLs. */
    cm_write_bypass(CM_BYPASS_PERPLL | CM_BYPASS_SDRPLL | CM_BYPASS_MAINPLL);

    /* 3. Assert bandgap power-down on all VCOs (reset values, REGEXTSEL cleared). */
    CM_MAINPLL_VCO = CM_VCO_RESET_VALUE & ~CM_VCO_REGEXTSEL;
    CM_PERPLL_VCO  = CM_VCO_RESET_VALUE & ~CM_VCO_REGEXTSEL;
    CM_SDRPLL_VCO  = CM_VCO_RESET_VALUE & ~CM_VCO_REGEXTSEL;

    /* 4. Set source muxes to reset values. */
    CM_PERPLL_SRC   = 0x00000015U;  /* CLKMGR_PERPLLGRP_SRC_RESET_VALUE */
    CM_MAINPLL_L4SRC = 0x00000000U; /* CLKMGR_MAINPLLGRP_L4SRC_RESET_VALUE */

    /* 5. Three reads ≈ 5µs delay at boot clock (25 MHz). */
    (void)CM_MAINPLL_VCO;
    (void)CM_PERPLL_VCO;
    (void)CM_SDRPLL_VCO;

    /* 6. Deassert BGPWRDN, set numerator/denominator. */
    CM_MAINPLL_VCO = main_vco;
    CM_PERPLL_VCO  = peri_vco;
    CM_SDRPLL_VCO  = sdr_vco;

    /* 7. Start 7µs timer (spin ≥ 175 cycles @ 25 MHz). */
    /* 8. Write clock dividers during the wait. */
    CM_MAINPLL_MPUCLK          = CFG_HPS_MAINPLLGRP_MPUCLK_CNT;
    CM_ALTR_MPUCLK             = CFG_HPS_ALTERAGRP_MPUCLK;
    CM_MAINPLL_MAINCLK         = CFG_HPS_MAINPLLGRP_MAINCLK_CNT;
    CM_MAINPLL_DBGATCLK        = CFG_HPS_MAINPLLGRP_DBGATCLK_CNT;
    CM_MAINPLL_CFGS2FUSER0CLK  = CFG_HPS_MAINPLLGRP_CFGS2FUSER0CLK_CNT;
    CM_PERPLL_EMAC0CLK         = CFG_HPS_PERPLLGRP_EMAC0CLK_CNT;
    CM_PERPLL_EMAC1CLK         = CFG_HPS_PERPLLGRP_EMAC1CLK_CNT;
    CM_MAINPLL_MAINQSPICLK     = CFG_HPS_MAINPLLGRP_MAINQSPICLK_CNT;
    CM_PERPLL_PERQSPICLK       = CFG_HPS_PERPLLGRP_PERQSPICLK_CNT;
    CM_MAINPLL_MAINNANDSDMMCCLK = CFG_HPS_MAINPLLGRP_MAINNANDSDMMCCLK_CNT;
    CM_PERPLL_PERNANDSDMMCCLK  = CFG_HPS_PERPLLGRP_PERNANDSDMMCCLK_CNT;
    CM_PERPLL_PERBASECLK       = CFG_HPS_PERPLLGRP_PERBASECLK_CNT;
    CM_PERPLL_S2FUSER1CLK      = CFG_HPS_PERPLLGRP_S2FUSER1CLK_CNT;

    /* 9. Wait out the 7µs (spin fills the gap conservatively). */
    spin(10000);

    /* 10. Enable VCOs. */
    CM_MAINPLL_VCO = main_vco | CM_VCO_EN;
    CM_PERPLL_VCO  = peri_vco | CM_VCO_EN;
    CM_SDRPLL_VCO  = sdr_vco  | CM_VCO_EN;

    /* 11. Write remaining dividers while PLLs lock. */
    CM_MAINPLL_MAINDIV  = maindiv;
    CM_MAINPLL_DBGDIV   = dbgdiv;
    CM_MAINPLL_TRACEDIV = tracediv;
    CM_PERPLL_DIV       = perdiv;
    CM_PERPLL_GPIODIV   = CFG_HPS_PERPLLGRP_GPIODIV_GPIODBCLK;

    /* 12. Wait for all three PLLs to lock. */
    cm_wait_lock();

    /* 13. Write SDRAM clock counters (before outreset toggle). */
    CM_SDRPLL_DDRDQSCLK   = ddrdqsclk   & 0x1FFU;
    CM_SDRPLL_DDR2XDQSCLK = ddr2xdqsclk & 0x1FFU;
    CM_SDRPLL_DDRDQCLK    = ddrdqclk    & 0x1FFU;
    CM_SDRPLL_S2FUSER2CLK = s2fuser2clk & 0x1FFU;

    /* 14. Assert then deassert OUTRESETALL on all three PLLs. */
    {
        uint32_t mv = CM_MAINPLL_VCO;
        uint32_t pv = CM_PERPLL_VCO;

        CM_MAINPLL_VCO = mv | CM_VCO_OUTRESETALL;
        CM_PERPLL_VCO  = pv | CM_VCO_OUTRESETALL;
        CM_SDRPLL_VCO  = sdr_vco | CM_VCO_EN | CM_VCO_OUTRESETALL;

        CM_MAINPLL_VCO = mv & ~CM_VCO_OUTRESETALL;
        CM_PERPLL_VCO  = pv & ~CM_VCO_OUTRESETALL;
        CM_SDRPLL_VCO  = sdr_vco | CM_VCO_EN;
    }

    /* 15. Update SDRAM phase counters (poll phase-busy first). */
    cm_write_phase(ddrdqsclk,   (volatile uint32_t*)(CM_BASE + 0xC8), CM_SDRPLL_PHASE_MASK);
    cm_write_phase(ddr2xdqsclk, (volatile uint32_t*)(CM_BASE + 0xCC), CM_SDRPLL_PHASE_MASK);
    cm_write_phase(ddrdqclk,    (volatile uint32_t*)(CM_BASE + 0xD0), CM_SDRPLL_PHASE_MASK);
    cm_write_phase(s2fuser2clk, (volatile uint32_t*)(CM_BASE + 0xD4), CM_SDRPLL_PHASE_MASK);

    /* 16. Take all PLLs out of bypass. */
    cm_write_bypass(0);

    /* 17. Clear safe mode. */
    cm_write_ctrl(CM_CTRL | CM_CTRL_SAFEMODE);

    /* 18. Update peripheral/L4 source muxes (now safe with clocks gated). */
    CM_PERPLL_SRC    = persrc;
    CM_MAINPLL_L4SRC = l4src;

    /* 19. Ungate all clocks. */
    CM_MAINPLL_EN = ~0U;
    CM_PERPLL_EN  = ~0U;
    CM_SDRPLL_EN  = ~0U;

    /* 20. Clear PLL loss-of-lock status (write 1 to clear). */
    CM_INTER = CM_INTER_SDRPLLLOST | CM_INTER_PERPLLLOST | CM_INTER_MAINPLLLOST;
}

/* ---- IOCSR scan chain ---- */

#define SCANMGR_MAX_DELAY   100U

static int scan_engine_idle(void)
{
    uint32_t iter = SCANMGR_MAX_DELAY;
    const uint32_t mask = SCANMGR_STAT_ACTIVE | SCANMGR_STAT_WFIFOCNT;
    do {
        if (!(SCANMGR_STAT & mask))
            return 0;
    } while (iter--);
    return -1;
}

#define JTAG_BP_INSN    (1U << 0)
#define JTAG_BP_TMS     (1U << 1)
#define JTAG_BP_PAYLOAD (1U << 2)
#define JTAG_BP_2BYTE   (1U << 3)
#define JTAG_BP_4BYTE   (1U << 4)

static void jtag_io(uint32_t flags, uint8_t iarg, uint32_t parg)
{
    uint32_t data = parg;
    if (flags & JTAG_BP_INSN) {
        data <<= 8;
        if (flags & JTAG_BP_TMS) {
            data |= (0U << 7);
            data |= iarg & 0x3FU;
            if (flags & JTAG_BP_PAYLOAD) data |= (1U << 6);
        } else {
            data |= (1U << 7);
            data |= iarg & 0x0FU;
            if (flags & JTAG_BP_PAYLOAD) data |= (1U << 4);
        }
    }
    if (flags & JTAG_BP_4BYTE)
        SCANMGR_FIFO_QUAD   = data;
    else if (flags & JTAG_BP_2BYTE)
        SCANMGR_FIFO_DOUBLE = data & 0xFFFFU;
    else
        SCANMGR_FIFO_SINGLE = data & 0xFFU;
}

/* Send instruction header + variable-length data payload. */
static int jtag_insn_data(uint8_t iarg, const unsigned long *data,
                          unsigned int dlen)
{
    unsigned int i, j;

    jtag_io(JTAG_BP_INSN | JTAG_BP_2BYTE, iarg, dlen - 1);

    for (i = 0; i < dlen / 32; i++)
        jtag_io(JTAG_BP_4BYTE, 0, data[i]);

    if ((dlen % 32) > 24) {
        jtag_io(JTAG_BP_4BYTE, 0, data[i]);
    } else if (dlen % 32) {
        for (j = 0; j < dlen % 32; j += 8)
            jtag_io(0, 0, (uint32_t)(data[i] >> j));
    }

    return scan_engine_idle();
}

static int program_chain(unsigned int chain_id,
                         const unsigned long *table,
                         unsigned int bits)
{
    unsigned int rem, idx = 0;
    int ret;

    /* Chain 3 (HIO): de-assert DLL reset before loading. */
    if (chain_id == 3)
        FRZCTRL_HIOCTRL &= ~FRZCTRL_HIOCTRL_DLLRST;

    ret = scan_engine_idle();
    if (ret) return ret;

    SCANMGR_EN |= (1U << chain_id);

    while (bits) {
        rem = (bits > 128) ? 128 : bits;
        ret = jtag_insn_data(0x0, &table[idx], rem);
        if (ret) goto err;
        bits -= rem;
        idx  += 4;
    }

    SCANMGR_EN &= ~(1U << chain_id);
    return 0;
err:
    SCANMGR_EN &= ~(1U << chain_id);
    return ret;
}

void scan_mgr_init(void)
{
    uint32_t v;

    /* Enable IOCSR programming writes */
    SYSMGR_ROMCODE_CTRL |= SYSMGR_ROMCODE_WARMRSTCFGIO;

    program_chain(0, iocsr_scan_chain0_table, CFG_HPS_IOCSR_SCANCHAIN0_LENGTH);
    program_chain(1, iocsr_scan_chain1_table, CFG_HPS_IOCSR_SCANCHAIN1_LENGTH);
    program_chain(2, iocsr_scan_chain2_table, CFG_HPS_IOCSR_SCANCHAIN2_LENGTH);
    program_chain(3, iocsr_scan_chain3_table, CFG_HPS_IOCSR_SCANCHAIN3_LENGTH);

    SYSMGR_ROMCODE_CTRL &= ~SYSMGR_ROMCODE_WARMRSTCFGIO;

    /* Full HIO thaw: mirrors sys_mgr_frzctrl_thaw_req() channel 3.
     * Enables DDR I/O OCT termination — required before PHY calibration. */
    FRZCTRL_SRC = 0;                                 /* SW-controlled FSM */
    /* DLLRST already cleared in program_chain(3) */
    FRZCTRL_HIOCTRL |= FRZCTRL_HIOCTRL_OCT_CALSTART; /* start OCT bias/cal */
    spin(10000);                                      /* ~40µs @ 25MHz eosc1 */
    v = FRZCTRL_HIOCTRL;
    v = (v | FRZCTRL_HIOCTRL_BUSHOLD | FRZCTRL_HIOCTRL_CFG)
      & ~FRZCTRL_HIOCTRL_OCTRST;
    FRZCTRL_HIOCTRL = v;
    spin(2000);                                       /* 33 intosc ≈ 1µs */
    FRZCTRL_HIOCTRL |= FRZCTRL_HIOCTRL_WKPULLUP | FRZCTRL_HIOCTRL_TRISTATE;
    FRZCTRL_HIOCTRL &= ~FRZCTRL_HIOCTRL_REGRST;
    FRZCTRL_HIOCTRL |= FRZCTRL_HIOCTRL_SLEW;
}

/* ---- SDRAM controller register load ---- */

/* ROWBITS errata: override to 19 so controller covers full 32-bit space. */
#define SDR_ERRATA_ROWS     19U

void sdram_ctrl_init(void)
{
    /* SDR controller + PHY are held in reset at power-on (PERMODRST bit 29 = 1).
     * Must deassert before any SDR register writes. */
    RSTMGR_PERMODRST &= ~RSTMGR_PERMODRST_SDR;

    SDR_CTRLCFG =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_MEMTYPE    <<  0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_MEMBL      <<  3) |
        (0U                                                <<  8) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_ECCEN      << 10) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_ECCCORREN  << 11) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_REORDEREN  << 15) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_STARVELIMIT<< 16) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_DQSTRKEN   << 22) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_CTRLCFG_NODMPINS   << 23);

    SDR_DRAMTIMING1 =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING1_TCWL <<  0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING1_AL   <<  4) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING1_TCL  <<  9) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING1_TRRD << 14) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING1_TFAW << 18) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING1_TRFC << 24);

    SDR_DRAMTIMING2 =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING2_IF_TREFI <<  0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING2_IF_TRCD  << 13) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING2_IF_TRP   << 17) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING2_IF_TWR   << 21) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING2_IF_TWTR  << 25);

    SDR_DRAMTIMING3 =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING3_TRTP <<  0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING3_TRAS <<  4) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING3_TRC  <<  9) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING3_TMRD << 15) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING3_TCCD << 19);

    SDR_DRAMTIMING4 =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING4_SELFRFSHEXIT <<  0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMTIMING4_PWRDOWNEXIT  << 10);

    SDR_LOWPWRTIMING =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_LOWPWRTIMING_AUTOPDCYCLES     <<  0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_LOWPWRTIMING_CLKDISABLECYCLES << 16);

    SDR_DRAMADDRW =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMADDRW_COLBITS           <<  0) |
        (SDR_ERRATA_ROWS                                             <<  5) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMADDRW_BANKBITS           << 10) |
        ((uint32_t)(CFG_HPS_SDR_CTRLCFG_DRAMADDRW_CSBITS - 1U)     << 13);

    SDR_DRAMIFWIDTH  = CFG_HPS_SDR_CTRLCFG_DRAMIFWIDTH_IFWIDTH;
    SDR_DRAMDEVWIDTH = CFG_HPS_SDR_CTRLCFG_DRAMDEVWIDTH_DEVWIDTH;

    SDR_STATICCFG =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_STATICCFG_MEMBL        << 0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_STATICCFG_USEECCASDATA << 2);

    SDR_CTRLWIDTH = CFG_HPS_SDR_CTRLCFG_CTRLWIDTH_CTRLWIDTH;

    SDR_PHYCTRL0 = CFG_HPS_SDR_CTRLCFG_PHYCTRL_PHYCTRL_0;

    SDR_CPORTWIDTH = CFG_HPS_SDR_CTRLCFG_CPORTWIDTH_CPORTWIDTH;
    SDR_CPORTWMAP  = CFG_HPS_SDR_CTRLCFG_CPORTWMAP_CPORTWMAP;
    SDR_CPORTRMAP  = CFG_HPS_SDR_CTRLCFG_CPORTRMAP_CPORTRMAP;
    SDR_RFIFOCMAP  = CFG_HPS_SDR_CTRLCFG_RFIFOCMAP_RFIFOCMAP;
    SDR_WFIFOCMAP  = CFG_HPS_SDR_CTRLCFG_WFIFOCMAP_WFIFOCMAP;
    SDR_CPORTRDWR  = CFG_HPS_SDR_CTRLCFG_CPORTRDWR_CPORTRDWR;

    SDR_DRAMODT =
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMODT_WRITE << 0) |
        ((uint32_t)CFG_HPS_SDR_CTRLCFG_DRAMODT_READ  << 4);

    SDR_LOWPWR_EQ    = ((uint32_t)CFG_HPS_SDR_CTRLCFG_LOWPWREQ_SELFRFSHMASK << 4);

    SDR_MP_PRIORITY  = CFG_HPS_SDR_CTRLCFG_MPPRIORITY_USERPRIORITY;
    SDR_MP_WEIGHT0   = CFG_HPS_SDR_CTRLCFG_MPWIEIGHT_0_STATICWEIGHT_31_0;
    SDR_MP_WEIGHT1   = ((uint32_t)CFG_HPS_SDR_CTRLCFG_MPWIEIGHT_1_STATICWEIGHT_49_32 << 0) |
                       ((uint32_t)CFG_HPS_SDR_CTRLCFG_MPWIEIGHT_1_SUMOFWEIGHT_13_0   << 18);
    SDR_MP_WEIGHT2   = CFG_HPS_SDR_CTRLCFG_MPWIEIGHT_2_SUMOFWEIGHT_45_14;
    SDR_MP_WEIGHT3   = CFG_HPS_SDR_CTRLCFG_MPWIEIGHT_3_SUMOFWEIGHT_63_46;
    SDR_MP_PACING0   = CFG_HPS_SDR_CTRLCFG_MPPACING_0_THRESHOLD1_31_0;
    SDR_MP_PACING1   = ((uint32_t)CFG_HPS_SDR_CTRLCFG_MPPACING_1_THRESHOLD1_59_32 << 0) |
                       ((uint32_t)CFG_HPS_SDR_CTRLCFG_MPPACING_1_THRESHOLD2_3_0   << 28);
    SDR_MP_PACING2   = CFG_HPS_SDR_CTRLCFG_MPPACING_2_THRESHOLD2_35_4;
    SDR_MP_PACING3   = CFG_HPS_SDR_CTRLCFG_MPPACING_3_THRESHOLD2_59_36;
    SDR_MP_THRESHOLD0 = CFG_HPS_SDR_CTRLCFG_MPTHRESHOLDRST_0_THRESHOLDRSTCYCLES_31_0;
    SDR_MP_THRESHOLD1 = CFG_HPS_SDR_CTRLCFG_MPTHRESHOLDRST_1_THRESHOLDRSTCYCLES_63_32;
    SDR_MP_THRESHOLD2 = CFG_HPS_SDR_CTRLCFG_MPTHRESHOLDRST_2_THRESHOLDRSTCYCLES_79_64;

    SDR_STATICCFG |= SDR_STATICCFG_APPLYCFG;
}
