#include "blc/blc_crc.h"

uint8_t blc_crc8(const void *data, size_t len)
{
    /* T[i] = i nhân không nhớ với 0x07 (i < 16, không cần rút gọn) */
    static const uint8_t T[16] = {
        0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
        0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    };
    const uint8_t *p = (const uint8_t *)data;
    uint8_t crc = 0x00u;
    while (len--) {
        crc ^= *p++;
        crc = (uint8_t)((uint8_t)(crc << 4) ^ T[crc >> 4]);
        crc = (uint8_t)((uint8_t)(crc << 4) ^ T[crc >> 4]);
    }
    return (uint8_t)(crc ^ 0x55u);
}

#ifdef BLC_CRC32_SOFTWARE

uint32_t blc_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

#else /* khối CRC phần cứng */

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_crc.h"

uint32_t blc_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_CRC);
    LL_CRC_SetPolynomialSize(CRC, LL_CRC_POLYLENGTH_32B);
    LL_CRC_SetPolynomialCoef(CRC, LL_CRC_DEFAULT_CRC32_POLY);
    LL_CRC_SetInitialData(CRC, LL_CRC_DEFAULT_CRC_INITVALUE);
    LL_CRC_SetInputDataReverseMode(CRC, LL_CRC_INDATA_REVERSE_BYTE);
    LL_CRC_SetOutputDataReverseMode(CRC, LL_CRC_OUTDATA_REVERSE_BIT);
    LL_CRC_ResetCRCCalculationUnit(CRC);

    /* Khối CRC xử lý word 32-bit từ MSB; REV_IN = byte đảo bit trong từng
     * byte, nên đưa word dạng big-endian (__REV) để thứ tự byte giống zlib. */
    while (((uintptr_t)p & 3u) != 0u && len != 0u) {
        LL_CRC_FeedData8(CRC, *p++);
        len--;
    }
    const uint32_t *w = (const uint32_t *)(const void *)p;
    while (len >= 4u) {
        LL_CRC_FeedData32(CRC, __REV(*w++));
        len -= 4u;
    }
    p = (const uint8_t *)w;
    while (len--) {
        LL_CRC_FeedData8(CRC, *p++);
    }
    return LL_CRC_ReadData32(CRC) ^ 0xFFFFFFFFu;
}

#endif
