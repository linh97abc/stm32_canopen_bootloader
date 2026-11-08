/**
 * @file pds_identity.h
 * @brief Identity CANopen dùng chung (0x1000, 0x1018) cho app và bootloader
 *        (design.md mục 4.1).
 *
 * TODO: điền giá trị thật. Vendor ID do CiA cấp.
 */
#ifndef PDS_IDENTITY_H
#define PDS_IDENTITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PDS_ID_DEVICE_TYPE
#define PDS_ID_DEVICE_TYPE     0x00000000UL   /* 0x1000 */
#endif
#ifndef PDS_ID_VENDOR_ID
#define PDS_ID_VENDOR_ID       0x00000000UL   /* 0x1018:01 */
#endif
#ifndef PDS_ID_PRODUCT_CODE
#define PDS_ID_PRODUCT_CODE    0x00000000UL   /* 0x1018:02 */
#endif
#ifndef PDS_ID_REVISION
#define PDS_ID_REVISION        0x00000000UL   /* 0x1018:03 */
#endif

/** 0x1018:04 = CRC32 của UID 96-bit (12 byte tại UID_BASE 0x1FFF7590). */
uint32_t pds_identity_serial(void);

#ifdef __cplusplus
}
#endif

#endif /* PDS_IDENTITY_H */
