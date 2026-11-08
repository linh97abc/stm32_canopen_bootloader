# Test trên PC:  make -C tests
ROOT    := ..
CC      ?= gcc
PYTHON  ?= python3
BUILD   := build
MCU     := STM32G474xx
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
           -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie \
           -D$(MCU) -DPDS_CRC32_SOFTWARE -D_GNU_SOURCE \
           -I$(ROOT)/common/include -I$(ROOT)/bootloader/src -I.

COMMON  := $(ROOT)/common/src/pds_config.c $(ROOT)/common/src/pds_crc.c \
           $(ROOT)/common/src/pds_app_header.c $(ROOT)/common/src/pds_secver.c fake_flash.c
BL      := $(addprefix $(ROOT)/bootloader/src/, sdo.c od.c program.c image.c crypto/sha256.c crypto/rsa2048.c)

all: run

$(BUILD)/pubkey.c: $(ROOT)/keys/dev_public.pem
	@mkdir -p $(BUILD)
	$(PYTHON) $(ROOT)/tools/pds_image.py pubkey-c --key $< --out $@

$(BUILD)/test_config: test_config.c $(COMMON)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_sim: test_sim.c sim_stubs.c $(COMMON) $(BL) $(BUILD)/pubkey.c
	$(CC) $(CFLAGS) $^ -o $@

images: $(BUILD)/pubkey.c
	$(PYTHON) gen_test_app.py --out $(BUILD) --key $(ROOT)/keys/dev_private.pem --tool $(ROOT)/tools/pds_image.py

run: $(BUILD)/test_config $(BUILD)/test_sim images
	$(BUILD)/test_config
	$(BUILD)/test_sim $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all run images clean
