# Build bootloader:  make MCU=STM32G474xx  [PUBKEY=keys/dev_public.pem]
MCU     ?= STM32G474xx
PUBKEY  ?= ../keys/dev_public.pem
BUILD   ?= build/$(MCU)

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
PYTHON  ?= python3

include ../common/blc_common.mk

SRCS := \
    src/startup.c src/main.c src/clock.c src/fdcan.c src/canopen.c src/sdo.c \
    src/od.c src/program.c src/image.c src/boot_jump.c src/crypto/sha256.c src/crypto/rsa2048.c \
    $(BLC_COMMON_SRCS)

GEN_PUBKEY := $(BUILD)/pubkey.c

CPUFLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
CFLAGS   := $(CPUFLAGS) -std=c11 -Os -g3 -Wall -Wextra -Wshadow -Wno-unused-parameter \
            -ffunction-sections -fdata-sections -fno-common -ffreestanding \
            -D$(MCU) -Isrc $(addprefix -I,$(BLC_COMMON_INCS)) $(EXTRA_CFLAGS)
LDFLAGS  := $(CPUFLAGS) -nostartfiles --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/bootloader.map -Wl,--print-memory-usage

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
