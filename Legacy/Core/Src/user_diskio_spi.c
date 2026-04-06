#include "stm32g0xx_hal.h"
#include "user_diskio_spi.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdbool.h>

extern SPI_HandleTypeDef hspi2; // <-- Change this to your actual SD SPI Handle (e.g. hspi1, hspi2)
#define SD_SPI_HANDLE hspi2     // <-- Link macro to handle

// --- SPI DMA CONFIGURATIONS ---
// Verify these channels match your CubeMX setup!
#define SPI_RX_DMA_CH DMA1_Channel1
#define SPI_RX_DMA_FLAG DMA_ISR_TCIF1

#define SPI_TX_DMA_CH DMA1_Channel2
#define SPI_TX_DMA_FLAG DMA_ISR_TCIF2

// Clear Global Interrupt Flags (Much safer than just TCIF)
#define DMA_CLEAR_RX_FLAGS DMA_IFCR_CGIF1
#define DMA_CLEAR_TX_FLAGS DMA_IFCR_CGIF2

static osSemaphoreId_t SpiDmaSemaphore = NULL;

void Init_SPI_DMA_Semaphore(void) {
    if (SpiDmaSemaphore == NULL) {
        const osSemaphoreAttr_t sem_attr = { .name = "SpiDmaSem" };
        SpiDmaSemaphore = osSemaphoreNew(1, 0, &sem_attr);
    }
}

/* --- OPTIMIZED SPI CLOCK SWITCHING --- */
#define FCLK_SLOW() { \
    __HAL_SPI_DISABLE(&SD_SPI_HANDLE); \
    MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_BAUDRATEPRESCALER_256, SPI_BAUDRATEPRESCALER_128); \
    __HAL_SPI_ENABLE(&SD_SPI_HANDLE); \
}
#define FCLK_FAST() { \
    __HAL_SPI_DISABLE(&SD_SPI_HANDLE); \
    MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_BAUDRATEPRESCALER_256, SPI_BAUDRATEPRESCALER_4	); \
    __HAL_SPI_ENABLE(&SD_SPI_HANDLE); \
}

#define CS_HIGH()   {HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);}
#define CS_LOW()    {HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);}

/* MMC/SD command 	*/
#define CMD0    (0)
#define CMD1    (1)
#define ACMD41  (0x80+41)
#define CMD8    (8)
#define CMD9    (9)
#define CMD10   (10)
#define CMD12   (12)
#define ACMD13  (0x80+13)
#define CMD16   (16)
#define CMD17   (17)
#define CMD18   (18)
#define CMD23   (23)
#define ACMD23  (0x80+23)
#define CMD24   (24)
#define CMD25   (25)
#define CMD32   (32)
#define CMD33   (33)
#define CMD38   (38)
#define CMD55   (55)
#define CMD58   (58)

#define CT_MMC      0x01
#define CT_SD1      0x02
#define CT_SD2      0x04
#define CT_SDC      (CT_SD1|CT_SD2)
#define CT_BLOCK    0x08

static volatile DSTATUS Stat = STA_NOINIT;
static BYTE CardType;

/* --- SMART RTOS DELAY --- */
static void safe_delay(uint32_t ms) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        osDelay(ms);
    } else {
        HAL_Delay(ms);
    }
}

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/

static BYTE xchg_spi(BYTE dat) {
    BYTE rxDat;
    HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &dat, &rxDat, 1, 50);
    return rxDat;
}

/* --- OPTIMIZED BULK READ --- */
static uint8_t dummy_ff = 0xFF;

static void rcvr_spi_multi(BYTE *buff, UINT btr) {
    // 1. Disable both DMA channels
    SPI_RX_DMA_CH->CCR &= ~DMA_CCR_EN;
    SPI_TX_DMA_CH->CCR &= ~DMA_CCR_EN;

    // 2. Clear all pending DMA flags safely
    DMA1->IFCR = DMA_CLEAR_RX_FLAGS | DMA_CLEAR_TX_FLAGS;

    // 3. Setup RX Channel (Memory Increment ON)
    SPI_RX_DMA_CH->CMAR = (uint32_t)buff;
    SPI_RX_DMA_CH->CPAR = (uint32_t)&(SD_SPI_HANDLE.Instance->DR);
    SPI_RX_DMA_CH->CNDTR = btr;

    // 4. Setup TX Channel (Memory Increment OFF)
    SPI_TX_DMA_CH->CMAR = (uint32_t)&dummy_ff;
    SPI_TX_DMA_CH->CPAR = (uint32_t)&(SD_SPI_HANDLE.Instance->DR);
    SPI_TX_DMA_CH->CNDTR = btr;
    SPI_TX_DMA_CH->CCR &= ~DMA_CCR_MINC;

    // 5. Enable DMA channels
    SPI_RX_DMA_CH->CCR |= DMA_CCR_EN;
    SPI_TX_DMA_CH->CCR |= DMA_CCR_EN;

    // 6. Enable SPI DMA Requests
    SD_SPI_HANDLE.Instance->CR2 |= (SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);

    // 7. Spin-lock wait with TIMEOUT to prevent Freezes
    uint32_t tickstart = HAL_GetTick();
    while (!(DMA1->ISR & SPI_RX_DMA_FLAG)) {
        if ((HAL_GetTick() - tickstart) > 500) {
            break; // TIMEOUT: Break out instead of freezing!
        }
    }

    // 8. Cleanup requests and channels
    SD_SPI_HANDLE.Instance->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    SPI_RX_DMA_CH->CCR &= ~DMA_CCR_EN;
    SPI_TX_DMA_CH->CCR &= ~DMA_CCR_EN;

    // 9. Restore TX DMA MINC
    SPI_TX_DMA_CH->CCR |= DMA_CCR_MINC;

    // 10. Wait for SPI bus to idle (with TIMEOUT)
    tickstart = HAL_GetTick();
    while (SD_SPI_HANDLE.Instance->SR & SPI_SR_BSY) {
        if ((HAL_GetTick() - tickstart) > 50) break;
    }
}

#if _USE_WRITE
static void xmit_spi_multi(const BYTE *buff, UINT btx) {
    // 1. Disable the DMA channel
    SPI_TX_DMA_CH->CCR &= ~DMA_CCR_EN;

    // 2. Clear any pending DMA flags safely
    DMA1->IFCR = DMA_CLEAR_TX_FLAGS;

    // 3. Set Memory Address and Transfer Length
    SPI_TX_DMA_CH->CMAR = (uint32_t)buff;
    SPI_TX_DMA_CH->CPAR = (uint32_t)&(SD_SPI_HANDLE.Instance->DR);
    SPI_TX_DMA_CH->CNDTR = btx;

    // 4. Enable DMA channel
    SPI_TX_DMA_CH->CCR |= DMA_CCR_EN;

    // 5. Enable SPI TX DMA Request
    SD_SPI_HANDLE.Instance->CR2 |= SPI_CR2_TXDMAEN;

    // 6. Spin-lock wait with TIMEOUT to prevent Freezes
    uint32_t tickstart = HAL_GetTick();
    while (!(DMA1->ISR & SPI_TX_DMA_FLAG)) {
        if ((HAL_GetTick() - tickstart) > 500) {
            break; // TIMEOUT: Break out instead of freezing!
        }
    }

    // 7. Cleanup
    SD_SPI_HANDLE.Instance->CR2 &= ~SPI_CR2_TXDMAEN;
    SPI_TX_DMA_CH->CCR &= ~DMA_CCR_EN;

    // 8. Wait for SPI busy flag to clear (with TIMEOUT)
    tickstart = HAL_GetTick();
    while (SD_SPI_HANDLE.Instance->SR & SPI_SR_BSY) {
        if ((HAL_GetTick() - tickstart) > 50) break;
    }
}
#endif

/*-----------------------------------------------------------------------*/
/* Wait for card ready (RTOS OPTIMIZED)                                  */
/*-----------------------------------------------------------------------*/
static int wait_ready(UINT wt) {
    BYTE d;
    uint32_t tickstart = HAL_GetTick();

    // Quick micro-spin for normal, fast busy-states
    for (uint32_t i = 0; i < 5000; i++) {
        d = xchg_spi(0xFF);
        if (d == 0xFF) return 1;
    }

    // If it takes longer than a few microseconds, then we yield
    do {
        d = xchg_spi(0xFF);
        if (d == 0xFF) return 1;
        safe_delay(1);
    } while ((HAL_GetTick() - tickstart) < wt);

    return 0; // Timeout
}

static void despiselect(void) {
    CS_HIGH();
    xchg_spi(0xFF);
}

static int spiselect(void) {
    CS_LOW();
    xchg_spi(0xFF);
    if (wait_ready(500)) return 1;
    despiselect();
    return 0;
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/
static int rcvr_datablock(BYTE *buff, UINT btr) {
    BYTE token;
    uint32_t tickstart = HAL_GetTick();

    do {
        token = xchg_spi(0xFF);
        if(token != 0xFF) break;

        if ((HAL_GetTick() - tickstart) > 1) {
            safe_delay(1);
        }
    } while ((HAL_GetTick() - tickstart) < 200);

    if(token != 0xFE) return 0;

    rcvr_spi_multi(buff, btr);
    xchg_spi(0xFF); xchg_spi(0xFF); // Discard CRC

    return 1;
}

/*-----------------------------------------------------------------------*/
/* Send a data packet to the MMC                                         */
/*-----------------------------------------------------------------------*/
#if _USE_WRITE
static int xmit_datablock(const BYTE *buff, BYTE token) {
    BYTE resp;

    if (!wait_ready(500)) return 0;

    xchg_spi(token);
    if (token != 0xFD) {
        xmit_spi_multi(buff, 512);
        xchg_spi(0xFF); xchg_spi(0xFF); // Dummy CRC

        resp = xchg_spi(0xFF);
        if ((resp & 0x1F) != 0x05) return 0;
    }
    return 1;
}
#endif

static BYTE send_cmd(BYTE cmd, DWORD arg) {
    BYTE n, res;

    if (cmd & 0x80) {
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }

    if (cmd != CMD12) {
        despiselect();
        if (!spiselect()) return 0xFF;
    }

    xchg_spi(0x40 | cmd);
    xchg_spi((BYTE)(arg >> 24));
    xchg_spi((BYTE)(arg >> 16));
    xchg_spi((BYTE)(arg >> 8));
    xchg_spi((BYTE)arg);

    n = 0x01;
    if (cmd == CMD0) n = 0x95;
    if (cmd == CMD8) n = 0x87;
    xchg_spi(n);

    if (cmd == CMD12) xchg_spi(0xFF);
    n = 10;
    do {
        res = xchg_spi(0xFF);
    } while ((res & 0x80) && --n);

    return res;
}

/*--------------------------------------------------------------------------
   Public FatFs Functions
---------------------------------------------------------------------------*/

inline DSTATUS USER_SPI_initialize(BYTE drv) {
    BYTE n, cmd, ty, ocr[4];
    uint32_t tickstart;

    Init_SPI_DMA_Semaphore();

    if (drv != 0) return STA_NOINIT;
    if (Stat & STA_NODISK) return Stat;

    FCLK_SLOW();
    for (n = 10; n; n--) xchg_spi(0xFF);

    ty = 0;
    if (send_cmd(CMD0, 0) == 1) {
        tickstart = HAL_GetTick();

        if (send_cmd(CMD8, 0x1AA) == 1) {
            for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                while (((HAL_GetTick() - tickstart) < 1000) && send_cmd(ACMD41, 1UL << 30)) safe_delay(1);

                if (((HAL_GetTick() - tickstart) < 1000) && send_cmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
                    ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;
                }
            }
        } else {
            if (send_cmd(ACMD41, 0) <= 1)   {
                ty = CT_SD1; cmd = ACMD41;
            } else {
                ty = CT_MMC; cmd = CMD1;
            }
            while (((HAL_GetTick() - tickstart) < 1000) && send_cmd(cmd, 0)) safe_delay(1);
            if (((HAL_GetTick() - tickstart) >= 1000) || send_cmd(CMD16, 512) != 0)
                ty = 0;
        }
    }
    CardType = ty;
    despiselect();

    if (ty) {
        FCLK_FAST();
        Stat &= ~STA_NOINIT;
    } else {
        Stat = STA_NOINIT;
    }

    return Stat;
}

inline DSTATUS USER_SPI_status(BYTE drv) {
    if (drv) return STA_NOINIT;
    return Stat;
}

inline DRESULT USER_SPI_read(BYTE drv, BYTE *buff, DWORD sector, UINT count) {
    if (drv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512;

    if (count == 1) {
        if ((send_cmd(CMD17, sector) == 0) && rcvr_datablock(buff, 512)) {
            count = 0;
        }
    } else {
        if (send_cmd(CMD18, sector) == 0) {
            do {
                if (!rcvr_datablock(buff, 512)) break;
                buff += 512;
            } while (--count);
            send_cmd(CMD12, 0);
        }
    }
    despiselect();
    return count ? RES_ERROR : RES_OK;
}

#if _USE_WRITE
inline DRESULT USER_SPI_write(BYTE drv, const BYTE *buff, DWORD sector, UINT count) {
    if (drv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (Stat & STA_PROTECT) return RES_WRPRT;

    if (!(CardType & CT_BLOCK)) sector *= 512;

    if (count == 1) {
        if ((send_cmd(CMD24, sector) == 0) && xmit_datablock(buff, 0xFE)) {
            count = 0;
        }
    } else {
        if (CardType & CT_SDC) send_cmd(ACMD23, count);
        if (send_cmd(CMD25, sector) == 0) {
            do {
                if (!xmit_datablock(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!xmit_datablock(0, 0xFD)) count = 1;
        }
    }
    despiselect();
    return count ? RES_ERROR : RES_OK;
}
#endif

#if _USE_IOCTL
inline DRESULT USER_SPI_ioctl(BYTE drv, BYTE cmd, void *buff) {
    DRESULT res;
    BYTE n, csd[16];
    DWORD *dp, st, ed, csize;

    if (drv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    res = RES_ERROR;

    switch (cmd) {
    case CTRL_SYNC:
        if (spiselect()) res = RES_OK;
        break;

    case GET_SECTOR_COUNT:
        if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
            if ((csd[0] >> 6) == 1) {
                csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                *(DWORD*)buff = csize << 10;
            } else {
                n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
                *(DWORD*)buff = csize << (n - 9);
            }
            res = RES_OK;
        }
        break;

    case GET_BLOCK_SIZE:
        if (CardType & CT_SD2) {
            if (send_cmd(ACMD13, 0) == 0) {
                xchg_spi(0xFF);
                if (rcvr_datablock(csd, 16)) {
                    for (n = 64 - 16; n; n--) xchg_spi(0xFF);
                    *(DWORD*)buff = 16UL << (csd[10] >> 4);
                    res = RES_OK;
                }
            }
        } else {
            if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
                if (CardType & CT_SD1) {
                    *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
                } else {
                    *(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
                }
                res = RES_OK;
            }
        }
        break;

    default:
        res = RES_PARERR;
    }

    despiselect();
    return res;
}
#endif

void USER_SPI_ResetStatus(void) {
    Stat = STA_NOINIT;
}
