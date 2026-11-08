#ifndef BL_PUBKEY_H
#define BL_PUBKEY_H

#include "crypto/rsa2048.h"

/** Public key nhúng trong bootloader, sinh bởi `tools/blc_image.py pubkey-c`. */
extern const rsa2048_pubkey_t g_bl_pubkey;

#endif
