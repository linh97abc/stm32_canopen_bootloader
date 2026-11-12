# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

CANopen (CiA 302-3) firmware-update bootloader for STM32G4 over FDCAN. Project docs, comments and commit messages are in Vietnamese; match that language. [README.md](README.md) covers the build/flash/master workflow, [docs/design.md](docs/design.md) is the detailed design (section numbers like "mục 5.5" are cited throughout code comments), and the README's table "Các điểm cụ thể hóa" lists where the code deliberately deviates from design.md.

## Commands

```sh
git submodule update --init --depth 1       # ST CMSIS/LL headers in third_party/ (required to build firmware)
pip install cryptography                    # for tools/blc_image.py

make -C bootloader MCU=STM32G474xx          # also STM32G431xx, STM32G491xx, ... -> bootloader/build/<MCU>/bootloader.{elf,bin,hex}
make -C bootloader PUBKEY=<path>/prod_public.pem   # embed a different signing public key (default: keys/dev_public.pem)

make -C tests                               # host-side tests (gcc + ASan/UBSan); keys are generated in tests/build/
tests/build/test_config                     # run one test binary directly (after `make -C tests`)
tests/build/test_sim tests/build            # takes the dir holding the generated test images
python3 tools/blc_image.py {keygen,pubkey-c,sign,info,verify} ...   # image signing tool
```

There is no linter or CI. The tests are the only automated check; they cover `common/` + the protocol/image logic of `bootloader/`, not the hardware drivers (FDCAN, clock, startup, flash programming).

## Architecture

Three flash regions (see `common/include/blc/blc_layout.h`, the single source of truth, preprocessed into the linker scripts via `gcc -E -P -x c -DBLC_LINKER_SCRIPT`): **bootloader (20 KB) → config (1 page) → app**. Page size and `BLC_APP_START` depend on the chip family (G431/G491: 2 KB pages; G47x/G48x: 4 KB pages and *requires* option byte DBANK=0 — `blc_layout_check()` gates all erase/write when it fails). Never hard-code addresses; derive them from `blc_layout.h`.

**`common/` (BLC library, `blc_`/`BLC_` prefix)** is linked into both the bootloader and the application (`blc_common.mk` or CMake `blc_common`). It owns everything the two sides must agree on: app header format and valid flag, config log in flash, App→bootloader handoff via `TAMP_BKP0R` magic, anti-rollback counter `SEC_MIN` in OTP, identity (0x1000/0x1018), flash write/erase and ECC-safe reads (NMI hook). Change a format here and both sides change together.

**`bootloader/src/`** layers, bottom to top: `fdcan` → `canopen` (NMT, boot-up, SDO dispatch, `co_process()` main loop) → `sdo` (expedited/segmented/block server) → `od` (object dictionary: 0x1000, 0x1018, 0x1F50:01 program data, 0x1F51:01 program control) → `program` (the download state machine: Clear/Start/Stop, streaming writes) → `image` (header/CRC/RSA-2048+SHA-256 verification). `main.c` boot path: take handoff request → if no request and app valid (`image_check_boot`: header + CRC + valid flag + vectors, **no signature check**) jump immediately with clocks still at reset state, else init clock/FDCAN and run CANopen. `boot_jump.c` and `startup.c` are target-only.

**Image/flash protocol invariants** (details in design.md section 5):
- `.blcimg` = app binary + `image_crc` + 256 B RSA signature. The 28-byte app header sits at `APP_START + 0x200`, left as zeros by the app's linker script and **filled entirely by `tools/blc_image.py sign`** (the app never declares it; read at runtime with `blc_app_header_in_flash()`).
- The signature is held in RAM only; the full signature check runs once on Start, over data re-read from flash. Only then is the `APPVALID` flag written and `SEC_MIN` raised.
- Erase is lazy: Clear erases only the first page (vectors + header, invalidating the old app); later pages are erased as the stream reaches them. The valid-flag page is erased before the header magic is written, so a stale flag at the same address cannot validate a new partial image.
- Because the bootloader can stall up to ~1 page erase per SDO block, response timing assumptions in the README table matter when touching `program.c`/`sdo.c`.
- The running app must handle `0x1F51:01` itself (Stop → wait for CAN TX FIFO empty, then `blc_boot_enter_bootloader()`; Start → OK; read → 1). See [examples/app/blc_app_glue.c](examples/app/blc_app_glue.c).

**Tests (`tests/`)** compile the real `common/` and `bootloader/` sources (software CRC32 via `-DBLC_CRC32_SOFTWARE`) against `fake_flash.c` (in-memory flash with ECC-error injection via `fake_flash_set_ecc()`) and `sim_stubs.c` (stubs for FDCAN/clock/jump). `test_sim.c` is a fake master driving the full Clear → download → Start flow; `gen_test_app.py` produces signed/corrupted images with the real signing tool. Hardware-facing files (`fdcan.c`, `clock.c`, `blc_flash_stm32g4.c`, `blc_boot.c`, ...) are not compiled for tests, so any function they provide that host-built code calls must be stubbed in `sim_stubs.c`.

## Gotchas

- `keys/dev_public.pem` is committed and is the default bootloader key; `keys/*_private.pem` is git-ignored. Tests never use `keys/`.
- `bootloader/src/pubkey.h` declares the key; the actual `pubkey.c` is generated into the build dir from a PEM by `blc_image.py pubkey-c`.
- Board-specific values (CAN pins, transceiver STBY pin, HSE frequency) live in `bootloader/src/board.h`; NodeID/bitrate defaults in `blc_config_ids.h`; override with `EXTRA_CFLAGS="-D..."`.
- The CPU runs at 150 MHz from HSI16 PLL; FDCAN is clocked from HSE (falls back to PCLK1). IWDG may be enabled by option byte, so any long loop must call `watchdog_kick()`.
