#include "blc/blc_identity.h"

#include "blc/blc_crc.h"
#include "stm32g4xx.h"

uint32_t blc_identity_serial(void)
{
    uint8_t uid[12];
    const volatile uint8_t *src = (const volatile uint8_t *)UID_BASE;
    for (unsigned i = 0; i < sizeof uid; i++) {
        uid[i] = src[i];
    }
    return blc_crc32(uid, sizeof uid);
}
