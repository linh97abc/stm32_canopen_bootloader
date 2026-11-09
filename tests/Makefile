# Test trên PC:  make -C tests
ROOT    := ..
CC      ?= gcc
PYTHON  ?= python3
BUILD   := build
MCU     := STM32G474xx
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
           -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie \
           -D$(MCU) -DBLC_CRC32_SOFTWARE -D_GNU_SOURCE \
           -I$(ROOT)/common/include -I$(ROOT)/bootloader/src -I.

COMMON  := $(ROOT)/common/src/blc_config.c $(ROOT)/common/src/blc_crc.c \
           $(ROOT)/common/src/blc_app_header.c $(ROOT)/common/src/blc_secver.c fake_flash.c
BL      := $(addprefix $(ROOT)/bootloader/src/, sdo.c od.c program.c image.c rescue.c crypto/sha256.c crypto/rsa2048.c)
# Header làm prerequisite: sửa .h (vd. blc_layout.h) thì test tự build lại.
HDRS    := $(wildcard $(ROOT)/common/include/blc/*.h $(ROOT)/bootloader/src/*.h \
                      $(ROOT)/bootloader/src/crypto/*.h *.h)

# Lấy BLC_APP_START từ blc_layout.h để không phải viết cứng địa chỉ ở nơi khác.
APP_START := $(shell echo BLC_APP_START | $(CC) -E -P -x c -DBLC_LINKER_SCRIPT -D$(MCU) \
                -I$(ROOT)/common/include -include blc/blc_layout.h -)

all: run

# Cặp khóa riêng cho test, sinh một lần trong build/, không dùng keys/.
TESTKEY := $(BUILD)/test_private.pem

$(TESTKEY):
	@mkdir -p $(BUILD)
	$(PYTHON) $(ROOT)/tools/blc_image.py keygen --out $(BUILD)/test

$(BUILD)/pubkey.c: $(TESTKEY)
	$(PYTHON) $(ROOT)/tools/blc_image.py pubkey-c --key $< --out $@

$(BUILD)/test_config: test_config.c $(COMMON) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(BUILD)/test_sim: test_sim.c sim_stubs.c $(COMMON) $(BL) $(BUILD)/pubkey.c $(HDRS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

images: $(BUILD)/pubkey.c
	$(PYTHON) gen_test_app.py --out $(BUILD) --key $(TESTKEY) --tool $(ROOT)/tools/blc_image.py --app-start $(APP_START)

run: $(BUILD)/test_config $(BUILD)/test_sim images
	$(BUILD)/test_config
	$(BUILD)/test_sim $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all run images clean
