# Thiết kế Bootloader

## 1. Tổng quan

| Hạng mục | Mô tả |
|---|---|
| MCU | STM32G4x |
| Giao tiếp nạp | CANopen trên FDCAN (theo CiA 302-3, Program Download) |
| Bảo mật | Firmware được ký bằng RSA-2048 |
| NodeID | App và bootloader dùng chung NodeID, lấy từ vùng config |

## 2. Phân vùng Flash

Mỗi vùng phải nằm trọn trong các page riêng, nên phân vùng phụ thuộc vào kích thước page của từng dòng chip.

**G431 / G441 / G491 / G4A1 (page 2 KB):**

| Vùng | Địa chỉ bắt đầu | Địa chỉ kết thúc | Kích thước |
|---|---|---|---|
| Bootloader | `0x0800_0000` | `0x0800_4FFF` | 20 KB (10 page) |
| Config | `0x0800_5000` | `0x0800_57FF` | 2 KB (1 page) |
| Application | `0x0800_5800` | Cuối flash | Tùy dòng chip |

**G47x / G48x, single-bank `DBANK = 0` (page 4 KB):**

| Vùng | Địa chỉ bắt đầu | Địa chỉ kết thúc | Kích thước |
|---|---|---|---|
| Bootloader | `0x0800_0000` | `0x0800_4FFF` | 20 KB (5 page) |
| Config | `0x0800_5000` | `0x0800_5FFF` | 4 KB (1 page) |
| Application | `0x0800_6000` | Cuối flash | Tùy dòng chip |

- Vùng config lưu cấu hình dùng chung cho cả app và bootloader.
- `APP_START` (`0x0800_5800` hoặc `0x0800_6000`) căn theo page, đủ để đặt `SCB->VTOR`.
- `APP_START`, `CONFIG_START` và `PAGE_SIZE` là hằng số theo dòng chip, đặt trong file header dùng chung cho app và bootloader. Linker script của app cũng dùng cùng giá trị.
- Với G47x/G48x phải giữ option byte `DBANK = 0`. Nếu chuyển sang dual-bank thì page đổi thành 2 KB và phân vùng sẽ sai.

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

### 4.1 Object dictionary của bootloader

Bootloader chỉ hỗ trợ các object bắt buộc theo CiA 301 và CiA 302-3. Riêng `0x1018` có đủ 4 sub, để master theo CiA 302-2 kiểm tra được đầy đủ identity khi node boot-up vào bootloader:

| Object | Tên | Truy cập | Theo | Ghi chú |
|---|---|---|---|---|
| `0x1000` | Device type | RO | CiA 301 | Hằng số, giống app |
| `0x1001` | Error register | RO | CiA 301 | Thường bằng `0`. Có thể bật bit 0 (generic error) khi không có app hợp lệ |
| `0x1017` | Producer heartbeat time | RW | CiA 301 | Error control dùng heartbeat, không hỗ trợ node guarding. Mặc định `0` (tắt) |
| `0x1018:00` | Identity: số sub-index | RO | CiA 301 | `4` |
| `0x1018:01` | Vendor ID | RO | CiA 301 | Giống app |
| `0x1018:02` | Product code | RO | CiA 301 | Giống app |
| `0x1018:03` | Revision number | RO | CiA 301 | Giống app |
| `0x1018:04` | Serial number | RO | CiA 301 | CRC32 của UID 96-bit của chip (`0x1FFF_7590`), app tính cùng cách |
| `0x1F50:01` | Program data | WO | CiA 302-3 | Nhận image (mục 5.4) |
| `0x1F51:01` | Program control | RW | CiA 302-3 | Ghi: `0` = Stop, `1` = Start, `3` = Clear |

- Các hằng số của `0x1000` và `0x1018:01–03`, cùng hàm tính serial number, đặt trong file header/source dùng chung với app. Như vậy identity của bootloader và app luôn giống hệt nhau.
- Không hỗ trợ (đều là tùy chọn): `0x100A`, `0x1F56`, `0x1F57`.
- Bootloader **không hỗ trợ LSS** (CiA 305). Nếu cần LSS thì app làm, rồi ghi NodeID/bitrate vào config. Bootloader chỉ đọc config.

## 5. Định dạng image và kiểm tra hợp lệ

### 5.1 Bố cục image

```
APP_START (mục 2)
├── 0x000  Vector table
├── 0x200  app_header_t (APP_START + 0x200)
├── ...    Code / data của app (đệm 0xFF cho tròn bội số 4 byte)
├── image_crc  (uint32, tại APP_START + image_size)
└── Valid flag (8 byte, tại ALIGN8(APP_START + image_size + 4), do bootloader ghi)
```

File image mà master gửi qua `0x1F50` gồm: **image + `image_crc` + chữ ký**. Chữ ký chỉ dùng để verify, nên bootloader giữ nó trong RAM và **không ghi vào flash**.

- Header đặt ở offset cố định `0x200` qua section `.app_header` trong linker script của app. Linker script chỉ chừa 28 byte 0, tool post-build điền toàn bộ các trường (`app_version`, `security_version` lấy từ tham số dòng lệnh).
- `image_size` tính từ `APP_START` đến hết app (có cả vector table và header). **Không** gồm `image_crc`. Phải là bội số của 4 để `image_crc` được căn 4 byte.
- `image_crc` nằm ngay sau image.

### 5.1.1 Valid flag

- **Địa chỉ:** `FLAG_ADDR = ALIGN8(APP_START + image_size + 4)`, tức double-word đầu tiên sau `image_crc`.
  - Phải căn 8 vì flash G4 ghi theo double-word 64-bit có ECC.
  - Nếu double-word chứa `image_crc` chỉ được ghi một phần (phần còn lại đệm `0xFF`) thì double-word đó không ghi lại được nữa. Căn lên 8 đảm bảo flag luôn nằm ở một double-word còn trống.
- **Không có trong file image.** Master không gửi flag. Bootloader tự ghi sau khi đã kiểm tra hết (mục 5.5).
- **Giá trị:**
  - Valid: `0x44494C41_56505041`, byte trong flash là `"APPVALID"`. Bootloader so khớp đủ 8 byte.
  - Chưa valid: `0xFFFF_FFFF_FFFF_FFFF` (flash đã xóa).
  - Thu hồi (tùy chọn): ghi toàn bộ `0x00`.
- **Bị xóa cùng app** khi master gửi Clear program, nên một app mới nạp dở không bao giờ còn mang cờ valid cũ.
- Cách này không dùng tới vùng config. Mất config cũng không làm mất trạng thái valid của app.

### 5.2 Cấu trúc header

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;          // 0x00: 0x48534450 (byte trong flash: "PDSH")
    uint16_t header_version; // 0x04: phiên bản định dạng header
    uint16_t sig_algo;       // 0x06: thuật toán ký xác thực
    uint32_t app_version;      // 0x08: major<<24 | minor<<16 | patch
    uint32_t security_version; // 0x0C: phiên bản bảo mật, chống rollback (mục 5.6)
    uint32_t build_time;       // 0x10: Unix time (giây, UTC)
    uint32_t image_size;       // 0x14: kích thước image (byte), bội số của 4
    uint32_t header_crc;       // 0x18: CRC32 của byte 0x00–0x17 của header
} app_header_t;                // 28 byte, little-endian
```

`image_crc` không nằm trong header mà ở cuối image (mục 5.1).

**`app_version` và `security_version`:**
- `app_version` chỉ để hiển thị và quản lý. Bootloader **không** hạn chế nạp bản cũ hơn theo trường này.
- `security_version` chỉ tăng khi bản mới vá một lỗ hổng bảo mật. Các bản không vá bảo mật giữ nguyên giá trị, nên vẫn hạ phiên bản qua lại được. Bản build đầu tiên dùng `0`.

**`sig_algo`:**

| Giá trị | Thuật toán | Bootloader bản đầu |
|---|---|---|
| `0x0001` | RSA-2048, SHA-256, PKCS#1 v1.5 | **Hỗ trợ** |
| `0x0002` | RSA-2048, SHA-256, PSS | Giữ chỗ, từ chối |
| Khác | Không định nghĩa | Từ chối |

- Bootloader chỉ chấp nhận các giá trị nằm trong danh sách mình hỗ trợ. **Không có giá trị nào nghĩa là "không ký"**, để không ai bỏ qua bước xác thực bằng cách sửa trường này.
- `sig_algo` cũng quyết định độ dài chữ ký ở cuối file image.

**Cách verify PKCS#1 v1.5:**
- Public exponent cố định `e = 65537`, hard-code trong bootloader cùng với modulus `n`.
- Tính `m = sig^e mod n`, rồi so `memcmp` nguyên khối 256 byte với chuỗi mong đợi do bootloader tự dựng: `00 01 FF…FF 00 || DigestInfo(SHA-256) || hash`.
- **Không parse ASN.1 hay padding từ chữ ký.** Cách dựng rồi so nguyên khối này chặn được tấn công giả chữ ký kiểu Bleichenbacher 2006.

### 5.3 Phạm vi các CRC và chữ ký

| Trường | Phạm vi tính |
|---|---|
| `header_crc` | 24 byte đầu của header (`magic` … `image_size`) |
| `image_crc` | Liền một mạch `[APP_START, APP_START + image_size)`, có cả header đã điền `header_crc` |
| Chữ ký | Mọi byte đứng trước nó: `[APP_START, APP_START + image_size + 4)`, tức image + `image_crc` |

- CRC32 dùng đa thức `0x04C11DB7`, init `0xFFFFFFFF`, đảo bit vào/ra, XOR cuối `0xFFFFFFFF` (giống CRC-32 của zlib). Tính bằng khối CRC phần cứng của G4: cấu hình `REV_IN` = byte, `REV_OUT` = 1, XOR cuối làm bằng phần mềm.
- Chữ ký phủ cả header, nên muốn sửa `sig_algo`, `security_version`… mà không làm hỏng chữ ký là không thể.
- Tool post-build trên PC lần lượt: đệm image cho tròn bội số 4 → điền toàn bộ header (`magic`, `header_version`, `sig_algo`, `app_version`, `security_version`, `build_time`, `image_size`) → tính `header_crc` → tính `image_crc` rồi nối vào cuối → ký → nối chữ ký vào cuối file.

### 5.4 Nhận image qua `0x1F50`

Bootloader nhận stream liên tục và chia theo offset:

| Offset trong stream | Xử lý |
|---|---|
| `[0, image_size + 4)` | Ghi vào flash từ `APP_START` |
| `[image_size + 4, image_size + 4 + sig_len)` | Copy vào buffer RAM (256 byte với RSA-2048) |
| Vượt quá | SDO abort |

- `image_size` và `sig_algo` lấy từ header. Header nằm ở offset `0x200–0x21B`, tới sớm trong stream, nên bootloader biết được ranh giới trước khi tới phần chữ ký.
- Khi nhận đủ header, kiểm tra ngay `magic`, `header_version`, `header_crc`, `sig_algo` và `security_version` (mục 5.6). Sai thì abort sớm, không cần chờ hết stream.

### 5.5 Kiểm tra hợp lệ

**Khi nhận lệnh Start program (sau khi nạp):**
1. Kiểm tra `magic` và `header_version` có hỗ trợ không, kiểm tra `header_crc`, kiểm tra `security_version ≥ SEC_MIN` (mục 5.6).
2. Kiểm tra `image_size` là bội số của 4. Kiểm tra `image_size + 4 +` độ dài chữ ký bằng số byte đã nhận qua `0x1F50`, và `FLAG_ADDR + 8` không vượt vùng app.
3. Tính CRC trên `[APP_START, APP_START + image_size)`, so với `image_crc` đọc tại `APP_START + image_size`.
4. Verify chữ ký trong buffer RAM theo `sig_algo`, tính trên dữ liệu **đọc lại từ flash** (`[APP_START, APP_START + image_size + 4)`). Như vậy là xác thực đúng những gì đã thực sự ghi vào flash. Public key lưu trong bootloader.
5. Cả 4 bước đều đạt thì ghi valid flag tại `FLAG_ADDR`.
6. Nếu `security_version > SEC_MIN` thì ghi entry mới vào OTP (mục 5.6).
7. Jump sang app.

**Mỗi lần boot bình thường:** chạy lại bước 1–3 (bước 2 chỉ kiểm tra giới hạn vùng app, vì lúc này không có số byte đã nhận). Sau đó kiểm tra valid flag tại `FLAG_ADDR`. Không verify chữ ký.

**Ghi chú:**
- G4 không có khối HASH/PKA, nên SHA-256 và RSA phải làm bằng phần mềm. Cần chọn thư viện gọn để vừa 20 KB.

### 5.6 Chống rollback bằng `security_version`

Mức tối thiểu `SEC_MIN` lưu trong OTP, để không bị mất khi mass erase, hạ RDP hay mất config.

**Vùng OTP dành cho bộ đếm:** 32 double-word đầu, `0x1FFF_7000–0x1FFF_70FF` (256 byte). Phần OTP còn lại để dùng cho việc khác.

**Mỗi entry (8 byte):**

| Byte | Nội dung |
|---|---|
| 0–3 | `security_version` |
| 4–7 | `~security_version` (đảo bit, để kiểm tra entry hợp lệ) |

**Đọc `SEC_MIN`:**
- Quét 32 entry. Entry toàn `0xFF` là còn trống.
- Entry hợp lệ là entry có byte 4–7 đúng bằng đảo bit của byte 0–3.
- `SEC_MIN` = giá trị lớn nhất trong các entry hợp lệ. Không có entry nào hợp lệ thì `SEC_MIN = 0`.
- Entry bị lỗi ECC (mất điện lúc ghi, gây NMI như mục 6.3) hoặc không hợp lệ thì bỏ qua và coi như đã dùng.

**Ghi `SEC_MIN` mới:**
- Bootloader tự nâng mức, chỉ sau khi image mới đã qua hết các bước kiểm tra và đã ghi valid flag (mục 5.5, bước 6).
- Ghi vào entry trống đầu tiên. Mỗi lần ghi có thể nhảy nhiều mức, ví dụ từ `0` lên thẳng `3`.
- Hết 32 entry thì không nâng được nữa. Bootloader vẫn chạy app bình thường, chỉ không tăng `SEC_MIN`, và không được ghi tràn sang vùng OTP khác.

**Lưu ý vận hành:**
- Một khi đã nạp bản có `security_version` cao hơn thì **không quay về được** bản có giá trị thấp hơn. Chỉ tăng trường này cho bản vá bảo mật, và phải test kỹ trước khi phát hành.
- Quy trình nạp xóa app (Clear program) **trước** khi nhận header. Nếu image bị từ chối vì `security_version` thấp, thiết bị sẽ đứng ở bootloader và không có app. Tool trên PC nên đọc header của file và cảnh báo trước khi nạp.

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
- **Không tìm thấy hoặc bị hỏng:** dùng giá trị mặc định hard-code trong firmware. "Hỏng" gồm 2 trường hợp:
  - Không có bản ghi nào của ID đó có CRC đúng.
  - Giá trị nằm ngoài miền hợp lệ: NodeID ngoài `1–127`, hoặc bitrate không thuộc danh sách hỗ trợ.
- Giá trị mặc định đặt trong một file header dùng chung cho app và bootloader, để hai bên luôn giống nhau.

### 6.3 Rủi ro mất điện khi gom page

Chỉ dùng 1 page để tiết kiệm flash. Như vậy chấp nhận rủi ro mất config nếu mất điện đúng lúc xóa page và ghi lại (thời gian xóa 1 page cỡ vài chục ms). Rủi ro được giảm như sau:

- **Ít khi phải gom page:** 1 page chứa được 256 bản ghi (page 2 KB) hoặc 512 bản ghi (page 4 KB). Chỉ ghi khi giá trị thực sự thay đổi, nên page rất lâu mới đầy.
- **Gom trong RAM:** đọc bản ghi mới nhất của từng ID vào RAM, xóa page rồi ghi lại ngay, giữ cửa sổ rủi ro ngắn nhất có thể.
- **Luôn khôi phục được:** nếu mất config thì mọi cấu hình về mặc định, kể cả NodeID và bitrate. Master vẫn kết nối lại được qua NodeID/bitrate mặc định để cấu hình lại.
- **Xử lý page bị hỏng do xóa dở:** đọc ô flash bị lỗi ECC 2-bit sẽ gây NMI (`FLASH_ECCR.ECCD`). Code đọc config phải bắt NMI này hoặc kiểm tra ECC. Nếu page lỗi thì xóa page và dùng mặc định.

### 6.4 Danh sách cấu hình

| ID | Cấu hình | Dùng bởi | Mặc định |
|---|---|---|---|
| TBD | CAN NodeID | App + Bootloader | TBD |
| TBD | CAN Bitrate | App + Bootloader | TBD |

## 7. Vấn đề cần chốt

- [x] Chế độ bank → G47x/G48x dùng single-bank, phân vùng theo page 4 KB (mục 2).
- [ ] Dòng chip G4 cụ thể (để chọn bảng phân vùng ở mục 2).
- [x] Định dạng image và vị trí header (mục 5).
- [x] Bootloader bản đầu hỗ trợ `sig_algo` nào → chỉ PKCS#1 v1.5 (`0x0001`). PSS giữ chỗ cho sau này (mục 5.2).
- [x] Giá trị `magic` → `0x48534450` ("PDSH"). Valid flag → `0x44494C4156505041` ("APPVALID").
- [x] Mỗi lần boot có verify lại chữ ký không → **Không**, chỉ kiểm tra cờ valid + CRC (mục 5).
- [x] Hành vi khi NodeID hoặc bitrate trong config bị hỏng → dùng giá trị mặc định hard-code (mục 6.2).
- [ ] Giá trị mặc định của NodeID và bitrate (mục 6.4).
- [x] Có hỗ trợ `0x1F56` (software identification) và `0x1F57` (flash status) không → **Không**, chỉ hỗ trợ object bắt buộc (mục 4.1).
- [x] Có cơ chế chống rollback không → **Có**, dùng `security_version` + bộ đếm trong OTP. `app_version` không bị hạn chế (mục 5.6).
