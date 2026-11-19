# Build bootloader:
#   make BOARD=<tên>                 board trong boards/<tên>.h (mặc định: generic)
#   make BOARD_FILE=<đường dẫn>.h    file board bất kỳ, kể cả ngoài repo
#   make MCU=STM32G431xx             chọn chip khi file board không định nghĩa BOARD_MCU
#   [PUBKEY=keys/dev_public.pem]  [EXTRA_CFLAGS="-D..."]
CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
PYTHON  ?= python3

BOARD      ?= generic
BOARD_FILE ?= boards/$(BOARD).h
BOARD_NAME := $(basename $(notdir $(BOARD_FILE)))

ifeq ($(wildcard $(BOARD_FILE)),)
$(error Không thấy file board '$(BOARD_FILE)'. Board có sẵn: $(basename $(notdir $(wildcard boards/*.h))))
endif

# Chip lấy từ BOARD_MCU trong file board (nếu có), qua chính preprocessor của gcc.
BOARD_MCU := $(strip $(shell $(CC) -E -dM -x c $(BOARD_FILE) 2>/dev/null | sed -n 's/^.define BOARD_MCU //p'))
ifneq ($(BOARD_MCU),)
ifeq ($(origin MCU),command line)
ifneq ($(MCU),$(BOARD_MCU))
$(error Board '$(BOARD_NAME)' dùng $(BOARD_MCU), không khớp MCU=$(MCU))
endif
endif
override MCU := $(BOARD_MCU)
endif
MCU     ?= STM32G474xx

PUBKEY  ?= ../keys/dev_public.pem
BUILD   ?= build/$(BOARD_NAME)/$(MCU)

include ../common/blc_common.mk

SRCS := \
    src/startup.c src/main.c src/clock.c src/fdcan.c src/canopen.c src/sdo.c \
    src/od.c src/program.c src/image.c src/boot_jump.c src/rescue.c src/crypto/sha256.c src/crypto/rsa2048.c \
    $(BLC_COMMON_SRCS)

GEN_PUBKEY := $(BUILD)/pubkey.c

CPUFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
CFLAGS   := $(CPUFLAGS) -std=c11 -Os -g3 -Wall -Wextra -Wshadow -Wno-unused-parameter \
            -ffunction-sections -fdata-sections -fno-common -ffreestanding \
            -D$(MCU) -DBOARD_FILE='"$(abspath $(BOARD_FILE))"' \
            -Isrc $(addprefix -I,$(BLC_COMMON_INCS)) $(EXTRA_CFLAGS)
LDFLAGS  := $(CPUFLAGS) -nostartfiles --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/bootloader.map -Wl,--print-memory-usage

# SHA-256 quét toàn image lúc Start, chiếm gần hết thời gian verify: tối ưu tốc độ.
$(BUILD)/obj/src/crypto/sha256.o: CFLAGS += -O3

OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(subst ../,,$(SRCS))) $(BUILD)/obj/pubkey.o
LDSCRIPT := $(BUILD)/bootloader.ld

all: $(BUILD)/bootloader.elf $(BUILD)/bootloader.bin $(BUILD)/bootloader.hex

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(GEN_PUBKEY): $(PUBKEY) ../tools/blc_image.py
	@mkdir -p $(dir $@)
	$(PYTHON) ../tools/blc_image.py pubkey-c --key $(PUBKEY) --out $@

$(BUILD)/obj/pubkey.o: $(GEN_PUBKEY)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LDSCRIPT): linker/bootloader.ld.S ../common/include/blc/blc_layout.h
	@mkdir -p $(dir $@)
	$(CC) -E -P -x c -DBLC_LINKER_SCRIPT -D$(MCU) -I../common/include $< -o $@

$(BUILD)/bootloader.elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -T$(LDSCRIPT) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD)/bootloader.bin: $(BUILD)/bootloader.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/bootloader.hex: $(BUILD)/bootloader.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf build

.PHONY: all clean
-include $(OBJS:.o=.d)
