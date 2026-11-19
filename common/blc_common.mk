# Thư viện dùng chung BLC - BootLoader Common (app + bootloader).
#
# Dùng trong Makefile:
#   include <đường dẫn>/common/blc_common.mk
#   SRCS     += $(BLC_COMMON_SRCS)
#   CFLAGS   += $(addprefix -I,$(BLC_COMMON_INCS)) $(BLC_COMMON_CFLAGS) -D$(MCU)
#
# Yêu cầu: define dòng chip CMSIS (vd. -DSTM32G474xx).
#
# Tùy chọn: BLC_BOARD_FILE = file board (vd. bootloader/boards/<tên>.h), đặt trước
# hoặc sau include đều được. File được -include vào mọi file nguồn, nên các giá trị
# hai bên phải giống nhau (NodeID/bitrate mặc định, identity, BLC_BOOT_BKP_INDEX...)
# lấy từ một chỗ. App và bootloader của cùng board nên dùng chung file này.

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

# "=" (không phải ":="): BLC_BOARD_FILE được đọc lúc dùng, có thể đặt sau include.
BLC_COMMON_CFLAGS = $(if $(BLC_BOARD_FILE),-include $(abspath $(BLC_BOARD_FILE)))
