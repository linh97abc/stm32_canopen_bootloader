# Thư viện dùng chung BLC - BootLoader Common (app + bootloader).
#
# Dùng trong Makefile:
#   include <đường dẫn>/common/blc_common.mk
#   SRCS     += $(BLC_COMMON_SRCS)
#   CFLAGS   += $(addprefix -I,$(BLC_COMMON_INCS)) -D$(MCU)
#
# Yêu cầu: define dòng chip CMSIS (vd. -DSTM32G474xx).

BLC_COMMON_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
BLC_ROOT       := $(BLC_COMMON_DIR)/..

BLC_COMMON_SRCS := \
    $(BLC_COMMON_DIR)/src/blc_layout.c \
    $(BLC_COMMON_DIR)/src/blc_flash_stm32g4.c \
    $(BLC_COMMON_DIR)/src/blc_crc.c \
    $(BLC_COMMON_DIR)/src/blc_config.c \
    $(BLC_COMMON_DIR)/src/blc_secver.c \
    $(BLC_COMMON_DIR)/src/blc_boot.c \
    $(BLC_COMMON_DIR)/src/blc_identity.c \
    $(BLC_COMMON_DIR)/src/blc_app_header.c

# Header CMSIS + LL lấy từ submodule chính gốc của ST (third_party/).
BLC_CMSIS_INCS := \
    $(BLC_ROOT)/third_party/cmsis_core/Include \
    $(BLC_ROOT)/third_party/cmsis_device_g4/Include \
    $(BLC_ROOT)/third_party/stm32g4xx_hal_driver/Inc

BLC_COMMON_INCS := $(BLC_COMMON_DIR)/include $(BLC_CMSIS_INCS)
