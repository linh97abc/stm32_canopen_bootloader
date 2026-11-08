#!/usr/bin/env python3
"""Tool post-build cho image app (bootloader/design.md mục 5).

Lệnh:
  keygen    --out keys/dev                  sinh cặp khóa RSA-2048 (PEM)
  pubkey-c  --key pub.pem --out pubkey.c    xuất public key cho bootloader
  sign      --key priv.pem --in app.bin --out app.blcimg
  info      --in app.blcimg [--sec-min N]   in header, cảnh báo rollback
  verify    --key pub.pem --in app.blcimg   kiểm tra lại như bootloader

`app.bin` là output `objcopy -O binary` của app, bắt đầu tại APP_START.
File .blcimg = image (đệm 0xFF cho tròn 4) + image_crc (LE) + chữ ký 256 byte.
Gửi nguyên file này vào 0x1F50:01.
"""
import argparse
import os
import struct
import sys
import time
import zlib

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

HEADER_OFFSET = 0x200
HEADER_FMT = "<IHHIIIII"           # 28 byte
HEADER_SIZE = struct.calcsize(HEADER_FMT)
HEADER_MAGIC = 0x48534450          # "PDSH"
HEADER_VERSION = 1
SIG_ALGO_RSA2048_PKCS1V15 = 0x0001
SIG_LEN = {SIG_ALGO_RSA2048_PKCS1V15: 256}

FIELDS = ("magic", "header_version", "sig_algo", "app_version",
          "security_version", "build_time", "image_size", "header_crc")


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_header(img: bytes) -> dict:
    if len(img) < HEADER_OFFSET + HEADER_SIZE:
        die("file quá ngắn, không có header tại 0x200")
    vals = struct.unpack_from(HEADER_FMT, img, HEADER_OFFSET)
    return dict(zip(FIELDS, vals))


def pack_header(h: dict) -> bytes:
    return struct.pack(HEADER_FMT, *(h[f] for f in FIELDS))


def load_private(path):
    with open(path, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(key, rsa.RSAPrivateKey) or key.key_size != 2048:
        die("cần khóa RSA-2048")
    return key


def load_public(path):
    with open(path, "rb") as f:
        data = f.read()
    try:
        key = serialization.load_pem_public_key(data)
    except ValueError:
        key = serialization.load_pem_private_key(data, password=None).public_key()
    if not isinstance(key, rsa.RSAPublicKey) or key.key_size != 2048:
        die("cần khóa RSA-2048")
    if key.public_numbers().e != 65537:
        die("bootloader chỉ hỗ trợ e = 65537")
    return key


def fmt_version(v):
    return f"{v >> 24}.{(v >> 16) & 0xFF}.{v & 0xFFFF}"


# ---------------------------------------------------------------- commands

def cmd_keygen(a):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    priv = a.out + "_private.pem"
    pub = a.out + "_public.pem"
    os.makedirs(os.path.dirname(priv) or ".", exist_ok=True)
    fd = os.open(priv, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM,
                                  serialization.PrivateFormat.PKCS8,
                                  serialization.NoEncryption()))
    with open(pub, "wb") as f:
        f.write(key.public_key().public_bytes(serialization.Encoding.PEM,
                                              serialization.PublicFormat.SubjectPublicKeyInfo))
    print(f"đã tạo {priv} và {pub}")


def cmd_pubkey_c(a):
    n = load_public(a.key).public_numbers().n
    words = 64
    r = 1 << (32 * words)
    rr = (r * r) % n
    n0inv = (-pow(n, -1, 1 << 32)) % (1 << 32)

    def limbs(x):
        return [(x >> (32 * i)) & 0xFFFFFFFF for i in range(words)]

    def arr(vals):
        lines = []
        for i in range(0, len(vals), 4):
            lines.append("        " + ", ".join(f"0x{v:08x}u" for v in vals[i:i + 4]) + ",")
        return "\n".join(lines)

    src = f"""/* File sinh tự động bởi tools/blc_image.py pubkey-c. KHÔNG sửa tay. */
#include "pubkey.h"

const rsa2048_pubkey_t g_bl_pubkey = {{
    .n = {{
{arr(limbs(n))}
    }},
    .rr = {{
{arr(limbs(rr))}
    }},
    .n0inv = 0x{n0inv:08x}u,
}};
"""
    with open(a.out, "w") as f:
        f.write(src)


def cmd_sign(a):
    with open(a.input, "rb") as f:
        img = bytearray(f.read())
    h = parse_header(img)
    if h["magic"] != HEADER_MAGIC:
        die(f"magic sai tại 0x200 (0x{h['magic']:08x}). App có BLC_APP_HEADER_DEFINE và section .app_header chưa?")
    if h["header_version"] != HEADER_VERSION:
        die(f"header_version {h['header_version']} không hỗ trợ")
    if h["security_version"] == 0xFFFFFFFF:
        die("security_version 0xFFFFFFFF không hợp lệ")

    while len(img) % 4:
        img.append(0xFF)

    h["sig_algo"] = SIG_ALGO_RSA2048_PKCS1V15
    h["image_size"] = len(img)
    if h["build_time"] == 0:
        h["build_time"] = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))
    h["header_crc"] = crc32(pack_header(h)[:24])
    img[HEADER_OFFSET:HEADER_OFFSET + HEADER_SIZE] = pack_header(h)

    body = bytes(img) + struct.pack("<I", crc32(bytes(img)))
    sig = load_private(a.key).sign(body, padding.PKCS1v15(), hashes.SHA256())
    with open(a.out, "wb") as f:
        f.write(body + sig)
    print(f"{a.out}: app {fmt_version(h['app_version'])}, security_version {h['security_version']}, "
          f"image_size {h['image_size']} B, tổng {len(body) + len(sig)} B")


def split_image(data: bytes):
    h = parse_header(data)
    size = h["image_size"]
    sig_len = SIG_LEN.get(h["sig_algo"], 0)
    if size % 4 or size + 4 + sig_len != len(data):
        die("độ dài file không khớp image_size/sig_algo")
    return h, data[:size], struct.unpack_from("<I", data, size)[0], data[size + 4:]


def cmd_info(a):
    with open(a.input, "rb") as f:
        data = f.read()
    h, img, img_crc, sig = split_image(data)
    print(f"magic            0x{h['magic']:08x}")
    print(f"header_version   {h['header_version']}")
    print(f"sig_algo         0x{h['sig_algo']:04x}")
    print(f"app_version      {fmt_version(h['app_version'])} (0x{h['app_version']:08x})")
    print(f"security_version {h['security_version']}")
    print(f"build_time       {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime(h['build_time']))}")
    print(f"image_size       {h['image_size']}")
    print(f"header_crc       0x{h['header_crc']:08x} ({'OK' if crc32(pack_header(h)[:24]) == h['header_crc'] else 'SAI'})")
    print(f"image_crc        0x{img_crc:08x} ({'OK' if crc32(img) == img_crc else 'SAI'})")
    if a.sec_min is not None:
        if h["security_version"] < a.sec_min:
            print(f"CẢNH BÁO: security_version {h['security_version']} < SEC_MIN {a.sec_min}: "
                  "bootloader sẽ từ chối, thiết bị sẽ không còn app sau Clear program!")
            sys.exit(2)
        if h["security_version"] > a.sec_min:
            print(f"LƯU Ý: nạp bản này sẽ nâng SEC_MIN lên {h['security_version']}, không hạ lại được.")


def cmd_verify(a):
    with open(a.input, "rb") as f:
        data = f.read()
    h, img, img_crc, sig = split_image(data)
    if crc32(pack_header(h)[:24]) != h["header_crc"]:
        die("header_crc sai")
    if crc32(img) != img_crc:
        die("image_crc sai")
    try:
        load_public(a.key).verify(sig, img + struct.pack("<I", img_crc), padding.PKCS1v15(), hashes.SHA256())
    except Exception:
        die("chữ ký sai")
    print("OK")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = p.add_subparsers(dest="cmd", required=True)

    s = sp.add_parser("keygen"); s.add_argument("--out", required=True); s.set_defaults(fn=cmd_keygen)
    s = sp.add_parser("pubkey-c"); s.add_argument("--key", required=True); s.add_argument("--out", required=True)
    s.set_defaults(fn=cmd_pubkey_c)
    s = sp.add_parser("sign"); s.add_argument("--key", required=True)
    s.add_argument("--in", dest="input", required=True); s.add_argument("--out", required=True)
    s.set_defaults(fn=cmd_sign)
    s = sp.add_parser("info"); s.add_argument("--in", dest="input", required=True)
    s.add_argument("--sec-min", type=int); s.set_defaults(fn=cmd_info)
    s = sp.add_parser("verify"); s.add_argument("--key", required=True)
    s.add_argument("--in", dest="input", required=True); s.set_defaults(fn=cmd_verify)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
