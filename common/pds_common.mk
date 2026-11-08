# Thư viện dùng chung PDS (app + bootloader).
#
# Dùng trong Makefile:
#   include <đường dẫn>/common/pds_common.mk
#   SRCS     += $(PDS_COMMON_SRCS)
#   CFLAGS   += $(addprefix -I,$(PDS_COMMON_INCS)) -D$(MCU)
#
# Yêu cầu: define dòng chip CMSIS (vd. -DSTM32G474xx).

PDS_COMMON_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
PDS_ROOT       := $(PDS_COMMON_DIR)/..

PDS_COMMON_SRCS := \
    $(PDS_COMMON_DIR)/src/pds_layout.c \
    $(PDS_COMMON_DIR)/src/pds_flash_stm32g4.c \
    $(PDS_COMMON_DIR)/src/pds_crc.c \
    $(PDS_COMMON_DIR)/src/pds_config.c \
    $(PDS_COMMON_DIR)/src/pds_secver.c \
    $(PDS_COMMON_DIR)/src/pds_boot.c \
    $(PDS_COMMON_DIR)/src/pds_identity.c \
    $(PDS_COMMON_DIR)/src/pds_app_header.c

# Header CMSIS + LL lấy từ submodule chính gốc của ST (third_party/).
PDS_CMSIS_INCS := \
    $(PDS_ROOT)/third_party/cmsis_core/Include \
    $(PDS_ROOT)/third_party/cmsis_device_g4/Include \
    $(PDS_ROOT)/third_party/stm32g4xx_hal_driver/Inc

PDS_COMMON_INCS := $(PDS_COMMON_DIR)/include $(PDS_CMSIS_INCS)
