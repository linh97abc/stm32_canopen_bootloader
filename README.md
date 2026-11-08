# PDS – Bootloader CANopen cho STM32G4

Thiết kế chi tiết: [bootloader/design.md](bootloader/design.md).

## Cấu trúc

```
common/              Thư viện dùng chung app + bootloader
  include/pds/
    pds_layout.h       Phân vùng flash theo dòng chip (dùng được trong linker script)
    pds_app_header.h   app_header_t, PDS_APP_HEADER_DEFINE(), valid flag, kiểm tra header
    pds_config.h       Lưu config dạng log append trong 1 page flash
    pds_config_ids.h   ID cấu hình + giá trị mặc định (NodeID, bitrate)
    pds_identity.h     Identity CANopen 0x1000/0x1018 + serial (CRC32 của UID)
    pds_boot.h         App -> bootloader qua backup register TAMP_BKP0R
    pds_secver.h       Bộ đếm chống rollback SEC_MIN trong OTP
    pds_flash.h        Ghi/xóa flash, đọc có kiểm tra ECC (NMI hook)
    pds_crc.h          CRC-32 (khối CRC phần cứng) / CRC-8
  pds_common.mk        Fragment cho Makefile
  CMakeLists.txt       Target `pds_common` cho dự án CMake
bootloader/          Firmware bootloader (≈ 9.7 KB / 24 KB)
  src/board.h          Chân CAN, tần số HSE  <-- chỉnh theo board
tools/pds_image.py   Ký image, xuất public key, xem header
examples/app/        Ví dụ tích hợp vào app + linker script mẫu
tests/               Test trên PC (flash giả, mô phỏng quy trình nạp CiA 302-3)
third_party/         Submodule chính gốc của ST: cmsis_core, cmsis_device_g4,
                     stm32g4xx_hal_driver (chỉ dùng phần LL)
```

## Chuẩn bị

```sh
git submodule update --init --depth 1
pip install cryptography            # cho tools/pds_image.py
python3 tools/pds_image.py keygen --out keys/dev   # khóa dev (private key bị .gitignore)
```

> Khóa production phải sinh và giữ ở nơi an toàn (HSM / máy ký riêng). Bootloader
> chỉ nhúng public key: `make PUBKEY=<đường dẫn>/prod_public.pem`.

## Build bootloader

```sh
make -C bootloader MCU=STM32G474xx      # hoặc STM32G431xx, STM32G491xx, ...
# -> bootloader/build/STM32G474xx/bootloader.{elf,bin,hex}
```

Ghi đè giá trị mặc định khi cần, ví dụ:
`make -C bootloader EXTRA_CFLAGS="-DPDS_CFG_DEFAULT_NODE_ID=10 -DBOARD_HSE_HZ=24000000"`.

**G47x/G48x:** option byte `DBANK` phải bằng `0`. Nếu không, bootloader không
nhảy sang app và từ chối Clear (abort `0x06060000`), để tránh xóa nhầm page.

## Test trên PC

```sh
make -C tests
```

- `test_config`: ghi/đọc, giá trị không đổi thì không ghi, page đầy thì gom lại,
  lỗi ECC ở ô cuối (mất điện lúc append) hoặc ở giữa page (mất điện lúc xóa),
  giá trị ngoài miền thì dùng mặc định.
- `test_sim`: master giả chạy toàn bộ quy trình Clear → nạp (block có CRC, có
  mất segment; segmented) → Start. Kiểm tra valid flag, CRC sai, sai khóa ký,
  nạp dở, file thừa/thiếu byte, chống rollback qua OTP, hết entry OTP, SDO timeout.

## Tích hợp vào app

Xem [examples/app/pds_app_glue.c](examples/app/pds_app_glue.c). Tóm tắt:

1. Thêm thư viện: `include common/pds_common.mk` (Makefile) hoặc
   `add_subdirectory(common)` (CMake), define dòng chip (`-DSTM32G474xx`).
2. Linker: `FLASH ORIGIN = PDS_APP_START`, thêm section `.app_header` tại
   `ORIGIN(FLASH) + 0x200` (mẫu: [examples/app/stm32g4_app.ld.S](examples/app/stm32g4_app.ld.S)).
3. `PDS_APP_HEADER_DEFINE(PDS_APP_VERSION(x, y, z), security_version);`
4. `VECT_TAB_OFFSET = PDS_APP_START - 0x08000000` trong `SystemInit`.
5. `NMI_Handler` gọi `pds_flash_nmi_handler()`.
6. NodeID/bitrate: `pds_config_init()`, `pds_config_node_id()`,
   `pds_config_bitrate_idx()`. Với LSS, lưu bằng `pds_config_set_*()`.
7. `0x1F51:01 = 0` (Stop program): gửi xong SDO response rồi gọi
   `pds_boot_enter_bootloader()`.
8. Không dùng `TAMP_BKP0R` (đổi được bằng `PDS_BOOT_BKP_INDEX`).
9. Post-build:
   ```sh
   arm-none-eabi-objcopy -O binary app.elf app.bin
   python3 tools/pds_image.py sign --key keys/dev_private.pem --in app.bin --out app.pdsimg
   python3 tools/pds_image.py info --in app.pdsimg --sec-min <SEC_MIN của thiết bị>
   ```
   Gửi `app.pdsimg` vào `0x1F50:01`.

## Quy trình nạp (master)

1. `0x1F51:01 = 0` → node reset vào bootloader và gửi boot-up.
2. `0x1F51:01 = 3` (Clear). Lệnh này chỉ xóa các page chưa trống, mỗi page mất
   khoảng 20–40 ms. **Timeout SDO của master nên ≥ 5 s.**
3. Tải `app.pdsimg` vào `0x1F50:01`. Ưu tiên SDO block (blksize 127, có CRC);
   segmented cũng hỗ trợ.
4. `0x1F51:01 = 1` (Start). Bootloader verify CRC + chữ ký RSA, ghi valid flag,
   nâng SEC_MIN nếu cần, trả response rồi reset vào app. Nếu image sai thì abort
   `0x08000020`. Nếu không có app hợp lệ hoặc nạp chưa xong thì abort `0x08000022`.

## Các điểm cụ thể hóa / khác với design.md

| Mục | Cách làm trong code | Lý do |
|---|---|---|
| 4 – ghi flash khi nhận block | Ghi flash **trước** rồi mới gửi block ACK | Client chờ ACK mới gửi block kế tiếp, nên lúc CPU stall không có frame nào tới. Nếu gửi ACK trước, block sau đến ngay trong lúc ghi (~10 ms/block) và sẽ tràn FIFO 3 phần tử |
| 4 – Clear | Chỉ xóa page chưa trống | Kết quả như "xóa toàn bộ" nhưng nhanh hơn nhiều |
| 5.5 bước 7 – Jump | Start → reset → boot path kiểm tra lại rồi nhảy | App khởi động với clock/ngoại vi ở trạng thái reset (app HAL hay giả định điều này) |
| 5.3 – CRC phần cứng | `REV_IN` = byte, đưa word qua `__REV()` | Cho kết quả giống zlib, test bằng giá trị check `0xCBF43926` |
| 6.1 – CRC-8 | CRC-8/I-432-1 (poly 0x07, xorout 0x55) | Bản ghi toàn `0x00` (đã xóa) không bao giờ có CRC đúng |
| 6.3 – page lỗi | Chỉ 1 ô lỗi ECC và nằm ở cuối log: ghi `0x00` để vô hiệu, giữ config. Lỗi ở chỗ khác: xóa page, dùng mặc định | Mất điện lúc append không làm mất toàn bộ config |
| 6.4 – ID | NodeID = `0x01`, bitrate = `0x02` (chỉ số CiA 305), app dùng từ `0x10` | Ghép thẳng được với LSS |
| Clock | CPU 150 MHz (PLL từ HSI16), FDCAN chạy bằng HSE | CAN cần clock chính xác. HSE không lên thì dùng PCLK1 |
| Watchdog | Nạp lại IWDG trong main loop và các vòng lặp dài | An toàn khi IWDG được bật bằng option byte |

## Việc còn mở (TODO)

- Chốt dòng chip (design.md mục 7). Code đã hỗ trợ cả hai bảng phân vùng.
- Giá trị mặc định NodeID / bitrate: tạm đặt `127` / 250 kbit/s trong `pds_config_ids.h`.
- Hằng số identity (`pds_identity.h`): device type, vendor ID, product code, revision.
- `bootloader/src/board.h`: chân CAN, chân STBY của transceiver, tần số HSE (tạm 8 MHz).
- Khuyến nghị: bật WRP option byte cho vùng bootloader (page 0–11 hoặc 0–5) khi sản xuất.
- Rủi ro: app hợp lệ nhưng không xử lý được Stop program thì không về được
  bootloader qua CAN. Có thể cân nhắc thêm cửa sổ chờ ngắn lúc boot.
