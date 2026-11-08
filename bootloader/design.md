# Thiết kế Bootloader

## 1. Tổng quan

| Hạng mục | Mô tả |
|---|---|
| MCU | STM32G4x |
| Giao tiếp nạp | CANopen trên FDCAN (theo CiA 302-3, Program Download) |
| Bảo mật | Firmware được ký bằng RSA-2048 |
| NodeID | App và bootloader dùng chung NodeID, lấy từ vùng config |

## 2. Phân vùng Flash

| Vùng | Địa chỉ bắt đầu | Địa chỉ kết thúc | Kích thước |
|---|---|---|---|
| Bootloader | `0x0800_0000` | `0x0800_5FFF` | 24 KB |
| Config | `0x0800_6000` | `0x0800_67FF` | 2 KB |
| Application | `0x0800_6800` | Cuối flash | Tùy dòng chip |

- Vùng config lưu cấu hình dùng chung cho cả app và bootloader.
- Địa chỉ app (`0x0800_6800`) căn theo 2 KB, đủ để đặt `SCB->VTOR`.

> ⚠️ **Lưu ý kích thước page:** G431/G441/G491 có page 2 KB. G47x/G48x ở chế độ single-bank (`DBANK = 0`) có page **4 KB**. Khi đó config và phần đầu app nằm chung page `0x0800_6000–0x0800_6FFF`, nên xóa config sẽ xóa luôn đầu app. Nếu dùng G47x/G48x thì phải bật dual-bank (page 2 KB) hoặc dời lại các vùng theo 4 KB.

## 3. Chuyển từ App sang Bootloader

1. App nhận lệnh **Stop program** (ghi `0x1F51:01 = 0`).
2. App ghi một giá trị magic vào backup register (`TAMP_BKPxR`), rồi gọi `NVIC_SystemReset()`.
3. Bootloader khởi động và đọc backup register:
   - **Có magic:** xóa magic, ở lại bootloader và gửi boot-up message (`0x700 + NodeID`, data `0x00`).
   - **Không có magic:** kiểm tra app hợp lệ (mục 5). Nếu hợp lệ thì jump sang app, nếu không thì ở lại bootloader.

> Backup register giữ được giá trị qua system reset. Trước khi ghi cần bật clock PWR và set bit `DBP` (`PWR_CR1`) để mở quyền ghi backup domain.

## 4. Quy trình nạp firmware (CiA 302-3)

| Bước | Master (PC) | Bootloader / App |
|---|---|---|
| 1 | Gửi **Stop program**: `0x1F51:01 = 0` | Nếu đang ở App thì chuyển về bootloader (mục 3) |
| 2 | Chờ boot-up message từ node | Bootloader gửi boot-up |
| 3 | Gửi **Clear program**: `0x1F51:01 = 3` | Xóa toàn bộ vùng app |
| 4 | Tải image vào `0x1F50:01` bằng **SDO block transfer** | Nhận dữ liệu và ghi vào flash |
| 5 | Gửi **Start program**: `0x1F51:01 = 1` | Kiểm tra CRC và chữ ký. Nếu đúng: đánh dấu valid rồi jump sang app. Nếu sai: báo SDO abort và ở lại bootloader |

**Lưu ý khi nhận SDO block:**
- Mỗi RX FIFO của FDCAN trên G4 chỉ có 3 phần tử. Bootloader phải lấy frame ra kịp, nên ưu tiên xử lý bằng ngắt.
- Khi ghi hoặc xóa flash, CPU bị stall (fetch từ cùng bank). Vì vậy nên gom một block vào RAM và chỉ ghi flash **giữa các block**, sau khi đã gửi block ACK. `blksize` chọn sao cho vừa buffer RAM (127 segment × 7 byte = 889 byte).

## 5. Định dạng image và kiểm tra hợp lệ

### 5.1 Bố cục image

```
APP_START (0x0800_6800)
├── 0x000  Vector table
├── 0x200  app_header_t (0x0800_6A00)
├── ...    Code / data của app
│          ← APP_START + image_size
└── Chữ ký (độ dài theo sig_algo, RSA-2048 = 256 byte)
```

- Header đặt ở offset cố định `0x200` qua section `.app_header` trong linker script của app.
- `image_size` tính từ `APP_START` đến hết app (có cả vector table và header), **không** gồm chữ ký.
- Chữ ký nằm ngay sau image, tại `APP_START + image_size`.

### 5.2 Cấu trúc header

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;          // 0x00: hằng số nhận diện header
    uint16_t header_version; // 0x04: phiên bản định dạng header
    uint16_t sig_algo;       // 0x06: thuật toán ký xác thực
    uint32_t app_version;    // 0x08: major<<24 | minor<<16 | patch
    uint32_t build_time;     // 0x0C: Unix time (giây, UTC)
    uint32_t image_size;     // 0x10: kích thước image (byte)
    uint32_t image_crc;      // 0x14: CRC32 của image, không tính header
    uint32_t header_crc;     // 0x18: CRC32 của byte 0x00–0x17 của header
} app_header_t;              // 28 byte, little-endian
```

**`sig_algo`:**

| Giá trị | Thuật toán |
|---|---|
| `0x0001` | RSA-2048, SHA-256, PKCS#1 v1.5 |
| `0x0002` | RSA-2048, SHA-256, PSS |
| Khác | Không hỗ trợ: bootloader từ chối image |

- Bootloader chỉ chấp nhận các giá trị nằm trong danh sách mình hỗ trợ. **Không có giá trị nào nghĩa là "không ký"**, để không ai bỏ qua bước xác thực bằng cách sửa trường này.
- `sig_algo` cũng quyết định độ dài chữ ký ở cuối image.

### 5.3 Phạm vi các CRC và chữ ký

| Trường | Phạm vi tính |
|---|---|
| `image_crc` | `[APP_START, header)` + `[header + 28, APP_START + image_size)`, tức bỏ qua header |
| `header_crc` | 24 byte đầu của header (`magic` … `image_crc`) |
| Chữ ký | Toàn bộ `[APP_START, APP_START + image_size)`, có cả header đã điền đủ CRC |

- CRC32 dùng đa thức `0x04C11DB7`, init `0xFFFFFFFF`, đảo bit vào/ra, XOR cuối `0xFFFFFFFF` (giống CRC-32 của zlib). Tính bằng khối CRC phần cứng của G4: cấu hình `REV_IN` = byte, `REV_OUT` = 1, XOR cuối làm bằng phần mềm.
- Chữ ký phủ cả header, nên muốn sửa `sig_algo`, `app_version`… mà không làm hỏng chữ ký là không thể.
- Tool post-build trên PC lần lượt: điền `image_size` → tính `image_crc` → tính `header_crc` → ký → nối chữ ký vào cuối file.

### 5.4 Kiểm tra hợp lệ

**Khi nhận lệnh Start program (sau khi nạp):**
1. Kiểm tra `magic` và `header_version` có hỗ trợ không, kiểm tra `header_crc`.
2. Kiểm tra `image_size` + độ dài chữ ký không vượt vùng app, và khớp với số byte đã nhận qua `0x1F50`.
3. Kiểm tra `image_crc`.
4. Verify chữ ký theo `sig_algo`. Public key lưu trong bootloader.
5. Cả 4 bước đều đạt thì ghi cờ valid vào config rồi jump sang app.

**Mỗi lần boot bình thường:** chạy lại bước 1–3 và kiểm tra cờ valid. Không verify chữ ký.

**Ghi chú:**
- G4 không có khối HASH/PKA, nên SHA-256 và RSA phải làm bằng phần mềm. Cần chọn thư viện gọn để vừa 24 KB.
- Cờ "valid" lưu dưới dạng một bản ghi trong vùng config (mục 6).

## 6. Cơ chế lưu config

### 6.1 Cấu trúc bản ghi (8 byte = 1 double-word flash)

| Byte | Nội dung |
|---|---|
| 0 | Tag: `0xFF` = ô trống, `0x00` = ô đã xóa, giá trị khác = ID cấu hình |
| 1–6 | Data (mỗi cấu hình tối đa 6 byte) |
| 7 | CRC-8 của byte 0–6 |

- Flash G4 ghi theo đơn vị 64-bit có ECC, nên mỗi bản ghi khớp đúng một lần ghi.
- Không bắt buộc đánh dấu bản ghi cũ là "đã xóa": khi đọc, bản ghi hợp lệ cuối cùng của mỗi ID đã được ưu tiên. Nếu muốn đánh dấu (tùy chọn) thì ghi cả 8 byte = `0x00`. Đây là trường hợp duy nhất flash G4 cho phép ghi đè một double-word đã ghi.

### 6.2 Quy tắc đọc và ghi

- **Ghi:** append bản ghi mới vào ô trống đầu tiên.
- **Đọc:** quét cả page, lấy bản ghi hợp lệ (CRC đúng) **cuối cùng** của mỗi ID.
- **Page đầy:** gom bản ghi mới nhất của từng ID, xóa page rồi ghi lại.
- **Không tìm thấy cấu hình:** dùng giá trị mặc định.

### 6.3 Rủi ro mất điện khi gom page

Chỉ dùng 1 page để tiết kiệm flash. Như vậy chấp nhận rủi ro mất config nếu mất điện đúng lúc xóa page và ghi lại (thời gian xóa 1 page cỡ vài chục ms). Rủi ro được giảm như sau:

- **Ít khi phải gom page:** 1 page 2 KB chứa được 256 bản ghi. Chỉ ghi khi giá trị thực sự thay đổi, nên page rất lâu mới đầy.
- **Gom trong RAM:** đọc bản ghi mới nhất của từng ID vào RAM, xóa page rồi ghi lại ngay, giữ cửa sổ rủi ro ngắn nhất có thể.
- **Luôn khôi phục được:** nếu mất config thì mọi cấu hình về mặc định, kể cả NodeID và bitrate. Master vẫn kết nối lại được qua NodeID/bitrate mặc định để cấu hình lại.
- **Xử lý page bị hỏng do xóa dở:** đọc ô flash bị lỗi ECC 2-bit sẽ gây NMI (`FLASH_ECCR.ECCD`). Code đọc config phải bắt NMI này hoặc kiểm tra ECC. Nếu page lỗi thì xóa page và dùng mặc định.

### 6.4 Danh sách cấu hình

| ID | Cấu hình | Dùng bởi | Mặc định |
|---|---|---|---|
| TBD | CAN NodeID | App + Bootloader | TBD |
| TBD | CAN Bitrate | App + Bootloader | TBD |
| TBD | App valid flag | Bootloader | Invalid |

## 7. Vấn đề cần chốt

- [ ] Dòng chip G4 cụ thể và chế độ bank (ảnh hưởng mục 2).
- [x] Định dạng image và vị trí header (mục 5).
- [ ] Bootloader bản đầu hỗ trợ `sig_algo` nào: PKCS#1 v1.5, PSS hay cả hai.
- [ ] Giá trị `magic`.
- [x] Mỗi lần boot có verify lại chữ ký không → **Không**, chỉ kiểm tra cờ valid + CRC (mục 5).
- [ ] Hành vi khi NodeID hoặc bitrate trong config bị hỏng.
- [ ] Có hỗ trợ `0x1F56` (software identification) và `0x1F57` (flash status) không.
- [ ] Có cơ chế chống rollback (so version) không.
