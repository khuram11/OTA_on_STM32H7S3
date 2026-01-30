/**
 * @file xspi2_flash_driver.c
 * @brief XSPI2 Flash Driver for MX25UW25645G on STM32H7S3/H7RS
 *        Based on RM0477 Reference Manual register definitions
 *
 * @target STM32H7S3L8H6H
 * @flash  MX25UW25645GXDI00 (256Mbit / 32MB) on XSPI2
 * 
 * USAGE:
 * 1. Add ONLY this file to your project
 * 2. Add .RamFunc section to linker script
 * 3. Call Flash_OTA_Write() for OTA updates
 */

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* Set to 1 for Octal DTR mode, 0 for standard SPI */
/* NOTE: Set this to match your flash's CURRENT mode (not memory-mapped config) */
#define USE_OCTAL_DTR_MODE      1

/* Set to 1 for Octal STR mode (8 lines, single transfer rate) */
#define USE_OCTAL_STR_MODE      0

/* ============================================================================
 * RAM PLACEMENT ATTRIBUTES
 * ============================================================================ */

#if defined(__GNUC__)
    #define RAM_FUNC    __attribute__((section(".RamFunc"), noinline, used))
    #define RAM_DATA    __attribute__((section(".RamData")))
#elif defined(__ICCARM__)
    #define RAM_FUNC    __ramfunc
    #define RAM_DATA    __no_init
#elif defined(__ARMCC_VERSION)
    #define RAM_FUNC    __attribute__((section("RamFunc")))
    #define RAM_DATA    __attribute__((section("RamData")))
#else
    #define RAM_FUNC
    #define RAM_DATA
#endif

/* ============================================================================
 * RETURN CODES
 * ============================================================================ */

typedef enum {
    FLASH_OK = 0,
    FLASH_ERROR_TIMEOUT,
    FLASH_ERROR_BUSY,
    FLASH_ERROR_WRITE_ENABLE,
    FLASH_ERROR_PROGRAM,
    FLASH_ERROR_ERASE,
    FLASH_ERROR_VERIFY,
    FLASH_ERROR_INVALID_PARAM
} FlashStatus_t;

/* ============================================================================
 * FLASH PARAMETERS - MX25UW25645G
 * ============================================================================ */

#define FLASH_PAGE_SIZE         256
#define FLASH_SECTOR_SIZE       4096
#define FLASH_BLOCK_64K_SIZE    65536
#define FLASH_TOTAL_SIZE        (32UL * 1024UL * 1024UL)  /* 32MB */

/* Timeouts (loop counts) */
#define TIMEOUT_DEFAULT         5000000UL
#define TIMEOUT_ERASE_SECTOR    100000000UL
#define TIMEOUT_ERASE_BLOCK     500000000UL

/* ============================================================================
 * XSPI2 REGISTER DEFINITIONS - From RM0477 Reference Manual
 * ============================================================================ */

/*
 * XSPI2 Base Address for STM32H7S3/H7RS:
 * PERIPH_BASE (0x40000000) + AHB5PERIPH_OFFSET (0x12000000) + XSPI2_OFFSET (0xA000)
 * = 0x5200A000
 */
#define XSPI2_BASE              0x5200A000UL

/* Register Offsets - From RM0477 Section 24.7 */
#define XSPI_CR_OFFSET          0x000U   /* Control Register */
#define XSPI_DCR1_OFFSET        0x008U   /* Device Configuration Register 1 */
#define XSPI_DCR2_OFFSET        0x00CU   /* Device Configuration Register 2 */
#define XSPI_DCR3_OFFSET        0x010U   /* Device Configuration Register 3 */
#define XSPI_DCR4_OFFSET        0x014U   /* Device Configuration Register 4 */
#define XSPI_SR_OFFSET          0x020U   /* Status Register */
#define XSPI_FCR_OFFSET         0x024U   /* Flag Clear Register */
#define XSPI_DLR_OFFSET         0x040U   /* Data Length Register */
#define XSPI_AR_OFFSET          0x048U   /* Address Register */
#define XSPI_DR_OFFSET          0x050U   /* Data Register */
#define XSPI_PSMKR_OFFSET       0x080U   /* Polling Status Mask Register */
#define XSPI_PSMAR_OFFSET       0x088U   /* Polling Status Match Register */
#define XSPI_PIR_OFFSET         0x090U   /* Polling Interval Register */
#define XSPI_CCR_OFFSET         0x100U   /* Communication Configuration Register */
#define XSPI_TCR_OFFSET         0x108U   /* Timing Configuration Register */
#define XSPI_IR_OFFSET          0x110U   /* Instruction Register */
#define XSPI_ABR_OFFSET         0x120U   /* Alternate Bytes Register */
#define XSPI_LPTR_OFFSET        0x130U   /* Low-Power Timeout Register */
#define XSPI_WPCCR_OFFSET       0x140U   /* Wrap Communication Configuration Register */
#define XSPI_WPTCR_OFFSET       0x148U   /* Wrap Timing Configuration Register */
#define XSPI_WPIR_OFFSET        0x150U   /* Wrap Instruction Register */
#define XSPI_WPABR_OFFSET       0x160U   /* Wrap Alternate Bytes Register */
#define XSPI_WCCR_OFFSET        0x180U   /* Write Communication Configuration Register */
#define XSPI_WTCR_OFFSET        0x188U   /* Write Timing Configuration Register */
#define XSPI_WIR_OFFSET         0x190U   /* Write Instruction Register */
#define XSPI_WABR_OFFSET        0x1A0U   /* Write Alternate Bytes Register */
#define XSPI_HLCR_OFFSET        0x200U   /* HyperBus Latency Configuration Register */

/* Register Access Macros */
#define XSPI2_REG(offset)       (*(volatile uint32_t *)(XSPI2_BASE + (offset)))

#define XSPI2_CR                XSPI2_REG(XSPI_CR_OFFSET)
#define XSPI2_DCR1              XSPI2_REG(XSPI_DCR1_OFFSET)
#define XSPI2_DCR2              XSPI2_REG(XSPI_DCR2_OFFSET)
#define XSPI2_DCR3              XSPI2_REG(XSPI_DCR3_OFFSET)
#define XSPI2_DCR4              XSPI2_REG(XSPI_DCR4_OFFSET)
#define XSPI2_SR                XSPI2_REG(XSPI_SR_OFFSET)
#define XSPI2_FCR               XSPI2_REG(XSPI_FCR_OFFSET)
#define XSPI2_DLR               XSPI2_REG(XSPI_DLR_OFFSET)
#define XSPI2_AR                XSPI2_REG(XSPI_AR_OFFSET)
#define XSPI2_DR                XSPI2_REG(XSPI_DR_OFFSET)
#define XSPI2_PSMKR             XSPI2_REG(XSPI_PSMKR_OFFSET)
#define XSPI2_PSMAR             XSPI2_REG(XSPI_PSMAR_OFFSET)
#define XSPI2_PIR               XSPI2_REG(XSPI_PIR_OFFSET)
#define XSPI2_CCR               XSPI2_REG(XSPI_CCR_OFFSET)
#define XSPI2_TCR               XSPI2_REG(XSPI_TCR_OFFSET)
#define XSPI2_IR                XSPI2_REG(XSPI_IR_OFFSET)
#define XSPI2_ABR               XSPI2_REG(XSPI_ABR_OFFSET)
#define XSPI2_LPTR              XSPI2_REG(XSPI_LPTR_OFFSET)
#define XSPI2_WPCCR             XSPI2_REG(XSPI_WPCCR_OFFSET)
#define XSPI2_WPTCR             XSPI2_REG(XSPI_WPTCR_OFFSET)
#define XSPI2_WPIR              XSPI2_REG(XSPI_WPIR_OFFSET)
#define XSPI2_WPABR             XSPI2_REG(XSPI_WPABR_OFFSET)
#define XSPI2_WCCR              XSPI2_REG(XSPI_WCCR_OFFSET)
#define XSPI2_WTCR              XSPI2_REG(XSPI_WTCR_OFFSET)
#define XSPI2_WIR               XSPI2_REG(XSPI_WIR_OFFSET)
#define XSPI2_WABR              XSPI2_REG(XSPI_WABR_OFFSET)
#define XSPI2_HLCR              XSPI2_REG(XSPI_HLCR_OFFSET)

/* Byte access for data register */
#define XSPI2_DR_BYTE           (*(volatile uint8_t *)(XSPI2_BASE + XSPI_DR_OFFSET))

/* ============================================================================
 * XSPI_CR - Control Register Bits (Section 24.7.1)
 * ============================================================================ */
#define XSPI_CR_EN              (1UL << 0)   /* Enable */
#define XSPI_CR_ABORT           (1UL << 1)   /* Abort request */
#define XSPI_CR_DMAEN           (1UL << 2)   /* DMA enable */
#define XSPI_CR_TCEN            (1UL << 3)   /* Timeout counter enable */
#define XSPI_CR_DMM             (1UL << 6)   /* Dual-memory mode */
#define XSPI_CR_FTHRES_POS      8            /* FIFO threshold position */
#define XSPI_CR_FTHRES_MSK      (0x1FUL << 8)
#define XSPI_CR_TEIE            (1UL << 16)  /* Transfer error interrupt enable */
#define XSPI_CR_TCIE            (1UL << 17)  /* Transfer complete interrupt enable */
#define XSPI_CR_FTIE            (1UL << 18)  /* FIFO threshold interrupt enable */
#define XSPI_CR_SMIE            (1UL << 19)  /* Status match interrupt enable */
#define XSPI_CR_TOIE            (1UL << 20)  /* Timeout interrupt enable */
#define XSPI_CR_APMS            (1UL << 22)  /* Automatic polling mode stop */
#define XSPI_CR_PMM             (1UL << 23)  /* Polling match mode */
#define XSPI_CR_CSSEL           (1UL << 24)  /* Chip select selection */
#define XSPI_CR_FMODE_POS       28           /* Functional mode position */
#define XSPI_CR_FMODE_MSK       (0x3UL << 28)

/* Functional modes */
#define FMODE_INDIRECT_WRITE    (0UL << 28)
#define FMODE_INDIRECT_READ     (1UL << 28)
#define FMODE_AUTO_POLLING      (2UL << 28)
#define FMODE_MEMORY_MAPPED     (3UL << 28)

/* ============================================================================
 * XSPI_SR - Status Register Bits (Section 24.7.6)
 * ============================================================================ */
#define XSPI_SR_TEF             (1UL << 0)   /* Transfer error flag */
#define XSPI_SR_TCF             (1UL << 1)   /* Transfer complete flag */
#define XSPI_SR_FTF             (1UL << 2)   /* FIFO threshold flag */
#define XSPI_SR_SMF             (1UL << 3)   /* Status match flag */
#define XSPI_SR_TOF             (1UL << 4)   /* Timeout flag */
#define XSPI_SR_BUSY            (1UL << 5)   /* Busy */
#define XSPI_SR_FLEVEL_POS      8
#define XSPI_SR_FLEVEL_MSK      (0x3FUL << 8)

/* ============================================================================
 * XSPI_FCR - Flag Clear Register Bits (Section 24.7.7)
 * ============================================================================ */
#define XSPI_FCR_CTEF           (1UL << 0)   /* Clear transfer error flag */
#define XSPI_FCR_CTCF           (1UL << 1)   /* Clear transfer complete flag */
#define XSPI_FCR_CSMF           (1UL << 3)   /* Clear status match flag */
#define XSPI_FCR_CTOF           (1UL << 4)   /* Clear timeout flag */

/* ============================================================================
 * XSPI_CCR - Communication Configuration Register Bits (Section 24.7.14)
 * ============================================================================ */
#define XSPI_CCR_IMODE_POS      0            /* Instruction mode position */
#define XSPI_CCR_IMODE_MSK      (0x7UL << 0)
#define XSPI_CCR_IDTR           (1UL << 3)   /* Instruction DTR mode */
#define XSPI_CCR_ISIZE_POS      4            /* Instruction size position */
#define XSPI_CCR_ISIZE_MSK      (0x3UL << 4)
#define XSPI_CCR_ADMODE_POS     8            /* Address mode position */
#define XSPI_CCR_ADMODE_MSK     (0x7UL << 8)
#define XSPI_CCR_ADDTR          (1UL << 11)  /* Address DTR mode */
#define XSPI_CCR_ADSIZE_POS     12           /* Address size position */
#define XSPI_CCR_ADSIZE_MSK     (0x3UL << 12)
#define XSPI_CCR_ABMODE_POS     16
#define XSPI_CCR_ABMODE_MSK     (0x7UL << 16)
#define XSPI_CCR_ABDTR          (1UL << 19)
#define XSPI_CCR_ABSIZE_POS     20
#define XSPI_CCR_ABSIZE_MSK     (0x3UL << 20)
#define XSPI_CCR_DMODE_POS      24           /* Data mode position */
#define XSPI_CCR_DMODE_MSK      (0x7UL << 24)
#define XSPI_CCR_DDTR           (1UL << 27)  /* Data DTR mode */
#define XSPI_CCR_DQSE           (1UL << 29)  /* DQS enable */

/* Line modes */
#define MODE_NONE               0UL
#define MODE_SINGLE             1UL
#define MODE_DUAL               2UL
#define MODE_QUAD               3UL
#define MODE_OCTAL              4UL

/* Sizes */
#define SIZE_8BIT               0UL
#define SIZE_16BIT              1UL
#define SIZE_24BIT              2UL
#define SIZE_32BIT              3UL

/* ============================================================================
 * XSPI_TCR - Timing Configuration Register Bits (Section 24.7.15)
 * ============================================================================ */
#define XSPI_TCR_DCYC_POS       0
#define XSPI_TCR_DCYC_MSK       (0x1FUL << 0)
#define XSPI_TCR_SSHIFT         (1UL << 30)

/* ============================================================================
 * MX25UW25645G FLASH COMMANDS
 * ============================================================================ */

#define MX25_CMD_WREN           0x06    /* Write Enable */
#define MX25_CMD_RDSR           0x05    /* Read Status Register */
#define MX25_CMD_PP_4B          0x12    /* Page Program with 4-byte address */
#define MX25_CMD_FAST_READ_4B   0x0C    /* Fast Read with 4-byte address */
#define MX25_CMD_SE_4B          0x21    /* Sector Erase 4KB */
#define MX25_CMD_BE64K_4B       0xDC    /* Block Erase 64KB */
#define MX25_CMD_CE             0x60    /* Chip Erase */
#define MX25_CMD_8DTRD          0xEE    /* Octal DTR Read */
#define MX25_CMD_8PP            0x12    /* Octal Page Program */

/* OPI (Octal) mode specific commands for MX25UW25645G */
#define MX25_CMD_WREN_OPI       0x06    /* Write Enable in OPI */
#define MX25_CMD_RDSR_OPI       0x05    /* Read Status in OPI (needs addr 0) */
#define MX25_CMD_SE_OPI         0x21    /* Sector Erase in OPI */
#define MX25_CMD_BE64K_OPI      0xDC    /* Block Erase 64K in OPI */
#define MX25_CMD_PP_OPI         0x12    /* Page Program in OPI */
#define MX25_CMD_8READ_OPI      0xEE    /* 8DTRD Read in OPI */

#define MX25_SR_WIP             0x01    /* Write In Progress */
#define MX25_SR_WEL             0x02    /* Write Enable Latch */

#define DUMMY_CYCLES_READ_DTR   20
#define DUMMY_CYCLES_REG_DTR    4
#define DUMMY_CYCLES_READ_SDR   8

/* ============================================================================
 * INTERNAL STATE (in RAM)
 * ============================================================================ */

RAM_DATA static uint32_t g_saved_cr;
RAM_DATA static uint32_t g_saved_dcr1;
RAM_DATA static uint32_t g_saved_dcr2;
RAM_DATA static uint32_t g_saved_dcr3;
RAM_DATA static uint32_t g_saved_dcr4;
RAM_DATA static uint32_t g_saved_ccr;
RAM_DATA static uint32_t g_saved_tcr;
RAM_DATA static uint32_t g_saved_ir;
RAM_DATA static uint32_t g_saved_abr;
RAM_DATA static uint32_t g_saved_lptr;
RAM_DATA static uint32_t g_saved_wpccr;
RAM_DATA static uint32_t g_saved_wptcr;
RAM_DATA static uint32_t g_saved_wpir;
RAM_DATA static uint32_t g_saved_wccr;
RAM_DATA static uint32_t g_saved_wtcr;
RAM_DATA static uint32_t g_saved_wir;
RAM_DATA static volatile uint8_t g_config_saved;
RAM_DATA static volatile uint8_t g_in_indirect;

/* ============================================================================
 * LOW-LEVEL HELPER FUNCTIONS
 * ============================================================================ */

RAM_FUNC static void delay_loop(volatile uint32_t count)
{
    while (count > 0) {
        __asm volatile ("nop");
        count--;
    }
}

RAM_FUNC static void mem_barrier(void)
{
    __asm volatile ("dsb sy" ::: "memory");
    __asm volatile ("isb sy" ::: "memory");
}

RAM_FUNC static uint32_t irq_disable(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    __asm volatile ("cpsid i" ::: "memory");
    return primask;
}

RAM_FUNC static void irq_restore(uint32_t primask)
{
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

RAM_FUNC static void clear_all_flags(void)
{
    XSPI2_FCR = XSPI_FCR_CTEF | XSPI_FCR_CTCF | XSPI_FCR_CSMF | XSPI_FCR_CTOF;
}

RAM_FUNC static FlashStatus_t wait_not_busy(uint32_t timeout)
{
    while ((XSPI2_SR & XSPI_SR_BUSY) && (timeout > 0)) {
        timeout--;
    }
    return (timeout > 0) ? FLASH_OK : FLASH_ERROR_TIMEOUT;
}

RAM_FUNC static FlashStatus_t wait_transfer_complete(uint32_t timeout)
{
    while (timeout > 0) {
        uint32_t sr = XSPI2_SR;
        if (sr & XSPI_SR_TEF) {
            clear_all_flags();
            return FLASH_ERROR_BUSY;
        }
        if (sr & XSPI_SR_TCF) {
            XSPI2_FCR = XSPI_FCR_CTCF;
            return FLASH_OK;
        }
        timeout--;
    }
    return FLASH_ERROR_TIMEOUT;
}

RAM_FUNC static FlashStatus_t xspi_abort(void)
{
    uint32_t timeout = TIMEOUT_DEFAULT;
    XSPI2_CR |= XSPI_CR_ABORT;
    while ((XSPI2_CR & XSPI_CR_ABORT) && (timeout > 0)) {
        timeout--;
    }
    clear_all_flags();
    return (timeout > 0) ? FLASH_OK : FLASH_ERROR_TIMEOUT;
}

/* Set functional mode - MUST wait for not busy first */
RAM_FUNC static FlashStatus_t set_fmode(uint32_t fmode)
{
    FlashStatus_t status = wait_not_busy(TIMEOUT_DEFAULT);
    if (status != FLASH_OK) return status;

    uint32_t cr = XSPI2_CR;
    cr &= ~XSPI_CR_FMODE_MSK;
    cr |= fmode;
    XSPI2_CR = cr;

    return FLASH_OK;
}

/* ============================================================================
 * CONFIGURATION SAVE/RESTORE
 * ============================================================================ */

RAM_FUNC static void save_config(void)
{
    if (!g_config_saved) {
        g_saved_cr    = XSPI2_CR;
        g_saved_dcr1  = XSPI2_DCR1;
        g_saved_dcr2  = XSPI2_DCR2;
        g_saved_dcr3  = XSPI2_DCR3;
        g_saved_dcr4  = XSPI2_DCR4;
        g_saved_ccr   = XSPI2_CCR;
        g_saved_tcr   = XSPI2_TCR;
        g_saved_ir    = XSPI2_IR;
        g_saved_abr   = XSPI2_ABR;
        g_saved_lptr  = XSPI2_LPTR;
        g_saved_wpccr = XSPI2_WPCCR;
        g_saved_wptcr = XSPI2_WPTCR;
        g_saved_wpir  = XSPI2_WPIR;
        g_saved_wccr  = XSPI2_WCCR;
        g_saved_wtcr  = XSPI2_WTCR;
        g_saved_wir   = XSPI2_WIR;
        g_config_saved = 1;
    }
}

RAM_FUNC static void restore_config(void)
{
    if (g_config_saved) {
        XSPI2_CR &= ~XSPI_CR_EN;
        while (XSPI2_CR & XSPI_CR_EN) { }

        XSPI2_DCR1  = g_saved_dcr1;
        XSPI2_DCR2  = g_saved_dcr2;
        XSPI2_DCR3  = g_saved_dcr3;
        XSPI2_DCR4  = g_saved_dcr4;
        XSPI2_CCR   = g_saved_ccr;
        XSPI2_TCR   = g_saved_tcr;
        XSPI2_IR    = g_saved_ir;
        XSPI2_ABR   = g_saved_abr;
        XSPI2_LPTR  = g_saved_lptr;
        XSPI2_WPCCR = g_saved_wpccr;
        XSPI2_WPTCR = g_saved_wptcr;
        XSPI2_WPIR  = g_saved_wpir;
        XSPI2_WCCR  = g_saved_wccr;
        XSPI2_WTCR  = g_saved_wtcr;
        XSPI2_WIR   = g_saved_wir;
        XSPI2_CR    = g_saved_cr;

        mem_barrier();
    }
}

/* ============================================================================
 * CCR CONFIGURATION HELPERS
 * ============================================================================ */

#if USE_OCTAL_DTR_MODE

/* Macronix Octal DTR: 16-bit instruction = CMD (high byte) + ~CMD (low byte)
 * Example: 0xEE becomes 0xEE11 (0xEE << 8 | 0x11 where 0x11 = ~0xEE)
 */
#define MAKE_DTR_CMD(cmd)   (((uint32_t)(cmd) << 8) | ((~(uint32_t)(cmd)) & 0xFF))

RAM_FUNC static void config_cmd_only_8dtr(void)
{
    /* Command only: Octal DTR instruction, no address, no data
     * Per RM0477: When ADMODE=000 and DMODE=000, transfer starts on IR write
     */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |  /* Instruction on 8 lines */
                XSPI_CCR_IDTR |                        /* Instruction DTR */
                (SIZE_16BIT << XSPI_CCR_ISIZE_POS) |   /* 16-bit instruction */
                (MODE_NONE << XSPI_CCR_ADMODE_POS) |   /* No address */
                (MODE_NONE << XSPI_CCR_DMODE_POS);     /* No data */
    XSPI2_TCR = 0;  /* No dummy cycles */
}

RAM_FUNC static void config_cmd_addr_8dtr(void)
{
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                XSPI_CCR_IDTR |
                (SIZE_16BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                XSPI_CCR_ADDTR |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_NONE << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_read_status_8dtr(void)
{
    /* Read Status Register in Octal DTR (matches EXTMEM config):
     * - 16-bit instruction (0x05FA)
     * - 32-bit address (0x00000000) - Macronix requires this
     * - 4 dummy cycles for status read (per MX25UW datasheet Table 27)
     * - Data on 8 lines, DTR mode
     */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                XSPI_CCR_IDTR |
                (SIZE_16BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                XSPI_CCR_ADDTR |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_DMODE_POS) |
                XSPI_CCR_DDTR;
    /* 4 dummy cycles for RDSR in OPI DTR mode, SSHIFT=1 */
    XSPI2_TCR = (4 << XSPI_TCR_DCYC_POS) | XSPI_TCR_SSHIFT;
}

RAM_FUNC static void config_write_data_8dtr(void)
{
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                XSPI_CCR_IDTR |
                (SIZE_16BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                XSPI_CCR_ADDTR |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_DMODE_POS) |
                XSPI_CCR_DDTR;
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_read_data_8dtr(void)
{
    /* Read Data in Octal DTR - matches captured CCR = 0x2C003C1C */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                XSPI_CCR_IDTR |
                (SIZE_16BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                XSPI_CCR_ADDTR |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_DMODE_POS) |
                XSPI_CCR_DDTR |
    			XSPI_CCR_DQSE;
    /* TCR = 0x1000000C: 12 dummy cycles, SSHIFT=1 */
    XSPI2_TCR = (12 << XSPI_TCR_DCYC_POS) | (1 << 28 ) ;
}

#elif USE_OCTAL_STR_MODE

/* Octal STR (Single Transfer Rate) mode - 8 lines, SDR */

RAM_FUNC static void config_cmd_only_8str(void)
{
    /* Command only in Octal STR: 8-bit instruction, no DTR */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |  /* Instruction on 8 lines */
                (SIZE_8BIT << XSPI_CCR_ISIZE_POS) |    /* 8-bit instruction */
                (MODE_NONE << XSPI_CCR_ADMODE_POS) |   /* No address */
                (MODE_NONE << XSPI_CCR_DMODE_POS);     /* No data */
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_cmd_addr_8str(void)
{
    /* Command + Address in Octal STR, no data (for erase) */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                (SIZE_8BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_NONE << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_read_status_8str(void)
{
    /* Read Status in Octal STR - no address needed, 8 dummy cycles */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                (SIZE_8BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_NONE << XSPI_CCR_ADMODE_POS) |   /* No address for STR status read */
                (MODE_OCTAL << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = (8 << XSPI_TCR_DCYC_POS) | XSPI_TCR_SSHIFT;
}

RAM_FUNC static void config_write_data_8str(void)
{
    /* Page Program in Octal STR */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                (SIZE_8BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_read_data_8str(void)
{
    /* Fast Read in Octal STR with dummy cycles */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                (SIZE_8BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = (20 << XSPI_TCR_DCYC_POS) | XSPI_TCR_SSHIFT;
}

#else /* Standard SPI Mode */

#define MAKE_DTR_CMD(cmd)   (cmd)

RAM_FUNC static void config_cmd_only_1spi(void)
{
    XSPI2_CCR = (MODE_SINGLE << XSPI_CCR_IMODE_POS);
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_cmd_addr_1spi(void)
{
    XSPI2_CCR = (MODE_SINGLE << XSPI_CCR_IMODE_POS) |
                (MODE_SINGLE << XSPI_CCR_ADMODE_POS) |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS);
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_read_status_1spi(void)
{
    XSPI2_CCR = (MODE_SINGLE << XSPI_CCR_IMODE_POS) |
                (MODE_SINGLE << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_write_data_1spi(void)
{
    XSPI2_CCR = (MODE_SINGLE << XSPI_CCR_IMODE_POS) |
                (MODE_SINGLE << XSPI_CCR_ADMODE_POS) |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_SINGLE << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = 0;
}

RAM_FUNC static void config_read_data_1spi(void)
{
    XSPI2_CCR = (MODE_SINGLE << XSPI_CCR_IMODE_POS) |
                (MODE_SINGLE << XSPI_CCR_ADMODE_POS) |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_SINGLE << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = (DUMMY_CYCLES_READ_SDR << XSPI_TCR_DCYC_POS) | XSPI_TCR_SSHIFT;
}

#endif /* USE_OCTAL_DTR_MODE */

/* ============================================================================
 * FLASH COMMAND IMPLEMENTATIONS
 * ============================================================================ */

RAM_FUNC static FlashStatus_t flash_write_enable(void)
{
    FlashStatus_t status;

    /* Ensure we're in indirect-write mode */
    status = set_fmode(FMODE_INDIRECT_WRITE);
    if (status != FLASH_OK) return status;

    clear_all_flags();
    
#if USE_OCTAL_DTR_MODE
    config_cmd_only_8dtr();
    XSPI2_IR = MAKE_DTR_CMD(MX25_CMD_WREN_OPI);
#elif USE_OCTAL_STR_MODE
    config_cmd_only_8str();
    XSPI2_IR = MX25_CMD_WREN;
#else
    config_cmd_only_1spi();
    XSPI2_IR = MX25_CMD_WREN;
#endif
    
    return wait_transfer_complete(TIMEOUT_DEFAULT);
}

RAM_FUNC static uint8_t flash_read_status(void)
{
    uint32_t data;
    
    /* Switch to indirect-read mode */
    if (set_fmode(FMODE_INDIRECT_READ) != FLASH_OK) {
        return 0xFF;
    }
    
    clear_all_flags();
    
#if USE_OCTAL_DTR_MODE
    config_read_status_8dtr();
    XSPI2_DLR = 1;  /* 2 bytes (DLR = length - 1) */
    XSPI2_IR = MAKE_DTR_CMD(MX25_CMD_RDSR_OPI);
    XSPI2_AR = 0x00000000;
#elif USE_OCTAL_STR_MODE
    config_read_status_8str();
    XSPI2_DLR = 0;  /* 1 byte */
    XSPI2_IR = MX25_CMD_RDSR;  /* No address needed, IR triggers */
#else
    config_read_status_1spi();
    XSPI2_DLR = 0;
    XSPI2_IR = MX25_CMD_RDSR;
#endif
    
    if (wait_transfer_complete(TIMEOUT_DEFAULT) != FLASH_OK) {
        set_fmode(FMODE_INDIRECT_WRITE);
        return 0xFF;
    }
    
    data = XSPI2_DR;

    /* Switch back to indirect-write mode */
    set_fmode(FMODE_INDIRECT_WRITE);

    return (uint8_t)(data & 0xFF);
}

/**
 * @brief Wait for flash to be ready using hardware auto-polling mode
 *        This matches ST's SAL_XSPI_CheckStatusRegister approach from the SFDP driver
 *        More efficient than software polling as it doesn't require CPU intervention
 */
RAM_FUNC static FlashStatus_t flash_wait_ready_autopoll(uint32_t timeout)
{
    FlashStatus_t status;

    /* Wait for XSPI not busy before changing mode */
    status = wait_not_busy(TIMEOUT_DEFAULT);
    if (status != FLASH_OK) return status;

    clear_all_flags();

    /* Configure for auto-polling mode */
    uint32_t cr = XSPI2_CR;
    cr &= ~XSPI_CR_FMODE_MSK;
    cr |= FMODE_AUTO_POLLING;    /* Set auto-polling mode */
    cr |= XSPI_CR_APMS;          /* Auto-polling mode stop on match */
    cr &= ~XSPI_CR_PMM;          /* Polling match mode: AND (all bits must match) */

#if USE_OCTAL_DTR_MODE
    /* Configure CCR for status read in 8D8D8D mode */
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                XSPI_CCR_IDTR |
                (SIZE_16BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_ADMODE_POS) |
                XSPI_CCR_ADDTR |
                (SIZE_32BIT << XSPI_CCR_ADSIZE_POS) |
                (MODE_OCTAL << XSPI_CCR_DMODE_POS) |
                XSPI_CCR_DDTR;
    XSPI2_TCR = (4 << XSPI_TCR_DCYC_POS) | XSPI_TCR_SSHIFT;

    XSPI2_DLR = 1;  /* 2 bytes in DTR mode */
    XSPI2_IR = MAKE_DTR_CMD(MX25_CMD_RDSR_OPI);

#elif USE_OCTAL_STR_MODE
    XSPI2_CCR = (MODE_OCTAL << XSPI_CCR_IMODE_POS) |
                (SIZE_8BIT << XSPI_CCR_ISIZE_POS) |
                (MODE_NONE << XSPI_CCR_ADMODE_POS) |
                (MODE_OCTAL << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = (8 << XSPI_TCR_DCYC_POS) | XSPI_TCR_SSHIFT;
    XSPI2_DLR = 0;
    XSPI2_IR = MX25_CMD_RDSR;

#else
    XSPI2_CCR = (MODE_SINGLE << XSPI_CCR_IMODE_POS) |
                (MODE_NONE << XSPI_CCR_ADMODE_POS) |
                (MODE_SINGLE << XSPI_CCR_DMODE_POS);
    XSPI2_TCR = 0;
    XSPI2_DLR = 0;
    XSPI2_IR = MX25_CMD_RDSR;
#endif

    /* Set polling mask and match: WIP bit (bit 0) must be 0 */
    XSPI2_PSMKR = MX25_SR_WIP;   /* Mask: check WIP bit */
    XSPI2_PSMAR = 0x00;          /* Match: WIP = 0 (not busy) */
    XSPI2_PIR = 0x10;            /* Polling interval */

    /* Apply configuration and start polling */
    XSPI2_CR = cr;

#if USE_OCTAL_DTR_MODE
    /* In modes with address, writing AR starts the operation */
    XSPI2_AR = 0x00000000;
#endif

    /* Wait for status match flag (SMF) */
    while (timeout > 0) {
        uint32_t sr = XSPI2_SR;
        if (sr & XSPI_SR_TEF) {
            clear_all_flags();
            set_fmode(FMODE_INDIRECT_WRITE);
            return FLASH_ERROR_BUSY;
        }
        if (sr & XSPI_SR_SMF) {
            XSPI2_FCR = XSPI_FCR_CSMF;
            set_fmode(FMODE_INDIRECT_WRITE);
            return FLASH_OK;
        }
        timeout--;
    }

    /* Timeout - abort and restore mode */
    xspi_abort();
    set_fmode(FMODE_INDIRECT_WRITE);
    return FLASH_ERROR_TIMEOUT;
}

/**
 * @brief Software polling fallback - less efficient but simpler
 */
RAM_FUNC static FlashStatus_t flash_wait_ready_sw(uint32_t timeout)
{
    while (timeout > 0) {
        uint8_t sr = flash_read_status();
        if (sr == 0xFF) return FLASH_ERROR_TIMEOUT;
        if (!(sr & MX25_SR_WIP)) return FLASH_OK;
        delay_loop(100);
        timeout--;
    }
    return FLASH_ERROR_TIMEOUT;
}

/**
 * @brief Wait for flash ready - uses hardware auto-polling for efficiency
 */
RAM_FUNC static FlashStatus_t flash_wait_ready(uint32_t timeout)
{
    /* Try hardware auto-polling first (more efficient) */
    FlashStatus_t status = flash_wait_ready_autopoll(timeout);

    /* Fall back to software polling if auto-poll fails */
    if (status != FLASH_OK && status != FLASH_ERROR_TIMEOUT) {
        status = flash_wait_ready_sw(timeout);
    }

    return status;
}

RAM_FUNC static FlashStatus_t flash_erase_sector(uint32_t address)
{
    FlashStatus_t status;
    
    address &= ~(FLASH_SECTOR_SIZE - 1);
    
    status = flash_write_enable();
    if (status != FLASH_OK) return status;
    
    if (!(flash_read_status() & MX25_SR_WEL)) {
        return FLASH_ERROR_WRITE_ENABLE;
    }
    
    /* Ensure we're in indirect-write mode */
    status = set_fmode(FMODE_INDIRECT_WRITE);
    if (status != FLASH_OK) return status;

    clear_all_flags();

#if USE_OCTAL_DTR_MODE
    config_cmd_addr_8dtr();
    XSPI2_IR = MAKE_DTR_CMD(MX25_CMD_SE_OPI);
    XSPI2_AR = address;
#elif USE_OCTAL_STR_MODE
    config_cmd_addr_8str();
    XSPI2_IR = MX25_CMD_SE_4B;
    XSPI2_AR = address;
#else
    config_cmd_addr_1spi();
    XSPI2_IR = MX25_CMD_SE_4B;
    XSPI2_AR = address;
#endif
    
    status = wait_transfer_complete(TIMEOUT_DEFAULT);
    if (status != FLASH_OK) return status;
    
    return flash_wait_ready(TIMEOUT_ERASE_SECTOR);
}

RAM_FUNC static FlashStatus_t flash_erase_block64k(uint32_t address)
{
    FlashStatus_t status;
    
    address &= ~(FLASH_BLOCK_64K_SIZE - 1);
    
    status = flash_write_enable();
    if (status != FLASH_OK) return status;
    
    if (!(flash_read_status() & MX25_SR_WEL)) {
        return FLASH_ERROR_WRITE_ENABLE;
    }
    
    /* Ensure we're in indirect-write mode */
    status = set_fmode(FMODE_INDIRECT_WRITE);
    if (status != FLASH_OK) return status;

    clear_all_flags();

#if USE_OCTAL_DTR_MODE
    config_cmd_addr_8dtr();
    XSPI2_IR = MAKE_DTR_CMD(MX25_CMD_BE64K_OPI);
    XSPI2_AR = address;
#elif USE_OCTAL_STR_MODE
    config_cmd_addr_8str();
    XSPI2_IR = MX25_CMD_BE64K_4B;
    XSPI2_AR = address;
#else
    config_cmd_addr_1spi();
    XSPI2_IR = MX25_CMD_BE64K_4B;
    XSPI2_AR = address;
#endif
    
    status = wait_transfer_complete(TIMEOUT_DEFAULT);
    if (status != FLASH_OK) return status;
    
    return flash_wait_ready(TIMEOUT_ERASE_BLOCK);
}

RAM_FUNC static FlashStatus_t flash_program_page(uint32_t address, const uint8_t *data, uint32_t length)
{
    FlashStatus_t status;
    uint32_t i;
    
    if (length == 0 || length > FLASH_PAGE_SIZE) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    uint32_t page_offset = address & (FLASH_PAGE_SIZE - 1);
    if ((page_offset + length) > FLASH_PAGE_SIZE) {
        length = FLASH_PAGE_SIZE - page_offset;
    }
    
    status = flash_write_enable();
    if (status != FLASH_OK) return status;
    
    if (!(flash_read_status() & MX25_SR_WEL)) {
        return FLASH_ERROR_WRITE_ENABLE;
    }
    
    /* Ensure we're in indirect-write mode */
    status = set_fmode(FMODE_INDIRECT_WRITE);
    if (status != FLASH_OK) return status;
    
    clear_all_flags();
    
#if USE_OCTAL_DTR_MODE
    config_write_data_8dtr();
    /* DTR mode transfers 16-bit words, round up to even */
    uint32_t dtr_length = (length + 1) & ~1UL;
    XSPI2_DLR = dtr_length - 1;
    XSPI2_IR = MAKE_DTR_CMD(MX25_CMD_PP_OPI);
    XSPI2_AR = address;

    /*
     * In DTR mode, bytes within each 16-bit word must be swapped when writing.
     * Write 16-bit words with bytes in correct order for flash.
     */
    volatile uint16_t *fifo16 = (volatile uint16_t *)(XSPI2_BASE + XSPI_DR_OFFSET);

    for (i = 0; i < length; i += 2) {
        uint32_t timeout = TIMEOUT_DEFAULT;
        while (!(XSPI2_SR & XSPI_SR_FTF) && (timeout > 0)) {
            if (XSPI2_SR & XSPI_SR_TEF) {
                clear_all_flags();
                return FLASH_ERROR_PROGRAM;
            }
            timeout--;
        }
        if (timeout == 0) return FLASH_ERROR_TIMEOUT;

        uint16_t word;
        uint8_t byte0 = data[i];
        uint8_t byte1 = (i + 1 < length) ? data[i + 1] : 0xFF;
        word = ((uint16_t)byte1 << 8) | byte0;
        *fifo16 = word;
    }

#elif USE_OCTAL_STR_MODE
    config_write_data_8str();
    XSPI2_DLR = length - 1;
    XSPI2_IR = MX25_CMD_PP_4B;
    XSPI2_AR = address;
    
    for (i = 0; i < length; i++) {
        uint32_t timeout = TIMEOUT_DEFAULT;
        while (!(XSPI2_SR & XSPI_SR_FTF) && (timeout > 0)) {
            if (XSPI2_SR & XSPI_SR_TEF) {
                clear_all_flags();
                return FLASH_ERROR_PROGRAM;
            }
            timeout--;
        }
        if (timeout == 0) return FLASH_ERROR_TIMEOUT;
        XSPI2_DR_BYTE = data[i];
    }
#else
    config_write_data_1spi();
    XSPI2_DLR = length - 1;
    XSPI2_IR = MX25_CMD_PP_4B;
    XSPI2_AR = address;
    
    for (i = 0; i < length; i++) {
        uint32_t timeout = TIMEOUT_DEFAULT;
        while (!(XSPI2_SR & XSPI_SR_FTF) && (timeout > 0)) {
            if (XSPI2_SR & XSPI_SR_TEF) {
                clear_all_flags();
                return FLASH_ERROR_PROGRAM;
            }
            timeout--;
        }
        if (timeout == 0) return FLASH_ERROR_TIMEOUT;
        XSPI2_DR_BYTE = data[i];
    }
#endif
    
    status = wait_transfer_complete(TIMEOUT_DEFAULT);
    if (status != FLASH_OK) return status;
    
    return flash_wait_ready(TIMEOUT_DEFAULT);
}

RAM_FUNC static FlashStatus_t flash_read(uint32_t address, uint8_t *data, uint32_t length)
{
    FlashStatus_t status;
    uint32_t i;
    
    if (length == 0) return FLASH_OK;
    
    /* Switch to indirect-read mode */
    status = set_fmode(FMODE_INDIRECT_READ);
    if (status != FLASH_OK) return status;
    
    clear_all_flags();
    
#if USE_OCTAL_DTR_MODE
    config_read_data_8dtr();
    /* DTR mode transfers 16-bit words, round up to even */
    uint32_t dtr_length = (length + 1) & ~1UL;
    XSPI2_DLR = dtr_length - 1;
    XSPI2_IR = MAKE_DTR_CMD(MX25_CMD_8READ_OPI);
    XSPI2_AR = address;

    /*
     * In DTR mode, bytes within each 16-bit word are swapped in the FIFO.
     * Read 16-bit words and swap bytes to get correct order.
     */
    volatile uint16_t *fifo16 = (volatile uint16_t *)(XSPI2_BASE + XSPI_DR_OFFSET);

    for (i = 0; i < length; i += 2) {
        uint32_t timeout = TIMEOUT_DEFAULT;
        while (!(XSPI2_SR & (XSPI_SR_FTF | XSPI_SR_TCF)) && (timeout > 0)) {
            if (XSPI2_SR & XSPI_SR_TEF) {
                clear_all_flags();
                set_fmode(FMODE_INDIRECT_WRITE);
                return FLASH_ERROR_BUSY;
            }
            timeout--;
        }
        if (timeout == 0) {
            set_fmode(FMODE_INDIRECT_WRITE);
            return FLASH_ERROR_TIMEOUT;
        }

        uint16_t word = *fifo16;

        data[i] = (uint8_t)(word & 0xFF);
        if (i + 1 < length) {
            data[i + 1] = (uint8_t)(word >> 8);
        }
    }

#elif USE_OCTAL_STR_MODE
    config_read_data_8str();
    XSPI2_DLR = length - 1;
    XSPI2_IR = MX25_CMD_FAST_READ_4B;
    XSPI2_AR = address;
    
    for (i = 0; i < length; i++) {
        uint32_t timeout = TIMEOUT_DEFAULT;
        while (!(XSPI2_SR & (XSPI_SR_FTF | XSPI_SR_TCF)) && (timeout > 0)) {
            if (XSPI2_SR & XSPI_SR_TEF) {
                clear_all_flags();
                set_fmode(FMODE_INDIRECT_WRITE);
                return FLASH_ERROR_BUSY;
            }
            timeout--;
        }
        if (timeout == 0) {
            set_fmode(FMODE_INDIRECT_WRITE);
            return FLASH_ERROR_TIMEOUT;
        }
        data[i] = XSPI2_DR_BYTE;
    }
#else
    config_read_data_1spi();
    XSPI2_DLR = length - 1;
    XSPI2_IR = MX25_CMD_FAST_READ_4B;
    XSPI2_AR = address;
    
    for (i = 0; i < length; i++) {
        uint32_t timeout = TIMEOUT_DEFAULT;
        while (!(XSPI2_SR & (XSPI_SR_FTF | XSPI_SR_TCF)) && (timeout > 0)) {
            if (XSPI2_SR & XSPI_SR_TEF) {
                clear_all_flags();
                set_fmode(FMODE_INDIRECT_WRITE);
                return FLASH_ERROR_BUSY;
            }
            timeout--;
        }
        if (timeout == 0) {
            set_fmode(FMODE_INDIRECT_WRITE);
            return FLASH_ERROR_TIMEOUT;
        }
        data[i] = XSPI2_DR_BYTE;
    }
#endif
    
    status = wait_transfer_complete(TIMEOUT_DEFAULT);

    /* Switch back to indirect-write mode */
    set_fmode(FMODE_INDIRECT_WRITE);

    return status;
}

/* ============================================================================
 * DIAGNOSTICS - Call Flash_ReadDiagnostics() BEFORE any flash ops to capture
 * the EXTMEM configuration. View g_xspi_diag in debugger.
 * Based on ST SFDP driver data structures for compatibility analysis.
 * ============================================================================ */
typedef struct {
    /* Raw register values */
    uint32_t CR;
    uint32_t DCR1;
    uint32_t DCR2;
    uint32_t DCR3;
    uint32_t DCR4;
    uint32_t SR;
    uint32_t CCR;
    uint32_t TCR;
    uint32_t IR;
    uint32_t ABR;
    uint32_t LPTR;
    uint32_t WCCR;
    uint32_t WTCR;
    uint32_t WIR;
    uint32_t WABR;

    /* Decoded values for easy debugging */
    struct {
        uint8_t fmode;        /* Functional mode: 0=ind-wr, 1=ind-rd, 2=poll, 3=mmap */
        uint8_t enabled;      /* XSPI enabled */
    } cr_decoded;

    struct {
        uint8_t imode;        /* Instruction mode: 0=none, 1=1-line, 4=8-line */
        uint8_t idtr;         /* Instruction DTR */
        uint8_t isize;        /* Instruction size: 0=8-bit, 1=16-bit */
        uint8_t admode;       /* Address mode */
        uint8_t addtr;        /* Address DTR */
        uint8_t adsize;       /* Address size: 3=32-bit */
        uint8_t dmode;        /* Data mode */
        uint8_t ddtr;         /* Data DTR */
    } ccr_decoded;

    struct {
        uint8_t dcyc;         /* Dummy cycles */
        uint8_t sshift;       /* Sample shift */
    } tcr_decoded;

    struct {
        uint16_t ir_cmd;      /* Instruction value (16-bit for DTR) */
        uint8_t cmd_byte;     /* High byte (command) */
        uint8_t cmd_inv;      /* Low byte (should be ~cmd for Macronix DTR) */
        uint8_t inverse_ok;   /* 1 if low byte = ~high byte */
    } ir_decoded;

} XSPI_DiagInfo_t;

volatile XSPI_DiagInfo_t g_xspi_diag;

/* Call this from main() BEFORE calling any flash write/erase functions
 * Then inspect g_xspi_diag in debugger to see what EXTMEM configured.
 *
 * Expected values for MX25UW25645G in Octal DTR mode:
 *   CCR = 0x2C003C1C (Octal DTR, 16-bit instruction, 32-bit address)
 *   TCR = 0x1000000C (12 dummy cycles for read, SSHIFT=1)
 *   IR  = 0xEE11     (8DTRD command: 0xEE + ~0xEE)
 */
void Flash_ReadDiagnostics(void)
{
    /* Read raw registers */
    g_xspi_diag.CR   = XSPI2_CR;
    g_xspi_diag.DCR1 = XSPI2_DCR1;
    g_xspi_diag.DCR2 = XSPI2_DCR2;
    g_xspi_diag.DCR3 = XSPI2_DCR3;
    g_xspi_diag.DCR4 = XSPI2_DCR4;
    g_xspi_diag.SR   = XSPI2_SR;
    g_xspi_diag.CCR  = XSPI2_CCR;
    g_xspi_diag.TCR  = XSPI2_TCR;
    g_xspi_diag.IR   = XSPI2_IR;
    g_xspi_diag.ABR  = XSPI2_ABR;
    g_xspi_diag.LPTR = XSPI2_LPTR;
    g_xspi_diag.WCCR = XSPI2_WCCR;
    g_xspi_diag.WTCR = XSPI2_WTCR;
    g_xspi_diag.WIR  = XSPI2_WIR;
    g_xspi_diag.WABR = XSPI2_WABR;

    /* Decode CR */
    g_xspi_diag.cr_decoded.enabled = (g_xspi_diag.CR & XSPI_CR_EN) ? 1 : 0;
    g_xspi_diag.cr_decoded.fmode = (g_xspi_diag.CR >> 28) & 0x3;

    /* Decode CCR - matches ST SFDP driver's JEDEC_Basic structure fields */
    g_xspi_diag.ccr_decoded.imode  = (g_xspi_diag.CCR >> 0) & 0x7;
    g_xspi_diag.ccr_decoded.idtr   = (g_xspi_diag.CCR >> 3) & 0x1;
    g_xspi_diag.ccr_decoded.isize  = (g_xspi_diag.CCR >> 4) & 0x3;
    g_xspi_diag.ccr_decoded.admode = (g_xspi_diag.CCR >> 8) & 0x7;
    g_xspi_diag.ccr_decoded.addtr  = (g_xspi_diag.CCR >> 11) & 0x1;
    g_xspi_diag.ccr_decoded.adsize = (g_xspi_diag.CCR >> 12) & 0x3;
    g_xspi_diag.ccr_decoded.dmode  = (g_xspi_diag.CCR >> 24) & 0x7;
    g_xspi_diag.ccr_decoded.ddtr   = (g_xspi_diag.CCR >> 27) & 0x1;

    /* Decode TCR */
    g_xspi_diag.tcr_decoded.dcyc   = (g_xspi_diag.TCR >> 0) & 0x1F;
    g_xspi_diag.tcr_decoded.sshift = (g_xspi_diag.TCR >> 30) & 0x1;

    /* Decode IR - check Macronix DTR command encoding */
    g_xspi_diag.ir_decoded.ir_cmd    = (uint16_t)(g_xspi_diag.IR & 0xFFFF);
    g_xspi_diag.ir_decoded.cmd_byte  = (g_xspi_diag.IR >> 8) & 0xFF;  /* High byte */
    g_xspi_diag.ir_decoded.cmd_inv   = g_xspi_diag.IR & 0xFF;         /* Low byte */
    /* For Macronix Octal DTR: low byte should be inverse of high byte */
    g_xspi_diag.ir_decoded.inverse_ok =
        ((~g_xspi_diag.ir_decoded.cmd_byte & 0xFF) == g_xspi_diag.ir_decoded.cmd_inv) ? 1 : 0;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

RAM_FUNC FlashStatus_t Flash_Init(void)
{
    g_config_saved = 0;
    g_in_indirect = 0;
    return FLASH_OK;
}

RAM_FUNC FlashStatus_t Flash_ExitMemoryMapped(void)
{
    if (g_in_indirect) return FLASH_OK;
    
    save_config();
    xspi_abort();
    
    XSPI2_CR &= ~XSPI_CR_EN;
    while (XSPI2_CR & XSPI_CR_EN) { }
    
    uint32_t cr = XSPI2_CR;
    cr &= ~XSPI_CR_FMODE_MSK;
    cr |= FMODE_INDIRECT_WRITE;
    cr |= XSPI_CR_EN;
    XSPI2_CR = cr;

    mem_barrier();

    g_in_indirect = 1;
    return FLASH_OK;
}

RAM_FUNC FlashStatus_t Flash_EnableMemoryMapped(void)
{
    if (!g_in_indirect) return FLASH_OK;
    
    wait_not_busy(TIMEOUT_DEFAULT);
    restore_config();
    
    g_in_indirect = 0;
    return FLASH_OK;
}

RAM_FUNC FlashStatus_t Flash_EraseSector4K(uint32_t address)
{
    return flash_erase_sector(address);
}

RAM_FUNC FlashStatus_t Flash_EraseBlock64K(uint32_t address)
{
    return flash_erase_block64k(address);
}

RAM_FUNC FlashStatus_t Flash_Program(uint32_t address, const uint8_t *data, uint32_t length)
{
    FlashStatus_t status;
    uint32_t written = 0;
    
    while (written < length) {
        uint32_t page_offset = (address + written) & (FLASH_PAGE_SIZE - 1);
        uint32_t chunk = FLASH_PAGE_SIZE - page_offset;
        if (chunk > (length - written)) {
            chunk = length - written;
        }
        
        status = flash_program_page(address + written, data + written, chunk);
        if (status != FLASH_OK) return status;
        
        written += chunk;
    }
    
    return FLASH_OK;
}

RAM_FUNC FlashStatus_t Flash_Read(uint32_t address, uint8_t *data, uint32_t length)
{
    return flash_read(address, data, length);
}

RAM_FUNC uint8_t Flash_IsBusy(void)
{
    return (flash_read_status() & MX25_SR_WIP) ? 1 : 0;
}

/* ============================================================================
 * MAIN OTA FUNCTION
 * ============================================================================ */

RAM_FUNC FlashStatus_t Flash_OTA_Write(uint32_t address, const uint8_t *data,
                                        uint32_t length, uint8_t erase_first)
{
    FlashStatus_t status;
    uint32_t primask;
    
    if (data == 0 || length == 0) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    if ((address + length) > FLASH_TOTAL_SIZE) {
        return FLASH_ERROR_INVALID_PARAM;
    }
    
    primask = irq_disable();
    
    status = Flash_ExitMemoryMapped();
    if (status != FLASH_OK) {
        irq_restore(primask);
        return status;
    }
    
    if (erase_first) {
        uint32_t erase_addr = address & ~(FLASH_SECTOR_SIZE - 1);
        uint32_t end_addr = address + length;
        
        while (erase_addr < end_addr) {
            if (((erase_addr & (FLASH_BLOCK_64K_SIZE - 1)) == 0) &&
                ((end_addr - erase_addr) >= FLASH_BLOCK_64K_SIZE)) {
                status = flash_erase_block64k(erase_addr);
                if (status != FLASH_OK) break;
                erase_addr += FLASH_BLOCK_64K_SIZE;
            } else {
                status = flash_erase_sector(erase_addr);
                if (status != FLASH_OK) break;
                erase_addr += FLASH_SECTOR_SIZE;
            }
        }
        
        if (status != FLASH_OK) {
            Flash_EnableMemoryMapped();
            irq_restore(primask);
            return status;
        }
    }
    
    status = Flash_Program(address, data, length);
    
    Flash_EnableMemoryMapped();
    irq_restore(primask);
    
    return status;
}

/* ============================================================================
 * TEST FUNCTION
 * ============================================================================ */

RAM_FUNC FlashStatus_t FlashTest_Basic(void)
{
    FlashStatus_t status;
    uint8_t write_buf[256];
    uint8_t read_buf[256];
    uint32_t test_addr = 0x01000000;
    uint32_t primask;
    uint32_t i;
    
    Flash_Init();
    
    for (i = 0; i < 256; i++) {
        write_buf[i] = (uint8_t)i;
        read_buf[i] = 0;
    }
    
    status = Flash_OTA_Write(test_addr, write_buf, 256, 1);
    if (status != FLASH_OK) return status;
    
    primask = irq_disable();
    Flash_ExitMemoryMapped();
    status = Flash_Read(test_addr, read_buf, 256);
    Flash_EnableMemoryMapped();
    irq_restore(primask);
    
    if (status != FLASH_OK) return status;
    
    for (i = 0; i < 256; i++) {
        if (read_buf[i] != write_buf[i]) {
            return FLASH_ERROR_VERIFY;
        }
    }
    
    return FLASH_OK;
}

RAM_FUNC int Test_Flash_Write(void)
{
    return (FlashTest_Basic() == FLASH_OK) ? 0 : -1;
}
