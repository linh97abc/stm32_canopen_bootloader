#!/usr/bin/env python3
"""Sinh image app giả (vector table + header + payload) và ký bằng tools/blc_image.py."""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

def make_bin(path, size, app_version, sec_version, seed, app_start):
    rnd = random.Random(seed)
    img = bytearray(rnd.getrandbits(8) for _ in range(size))
    struct.pack_into("<II", img, 0, 0x20008000, app_start + 0x401)  # SP, Reset (Thumb)
    hdr = struct.pack("<IHHIIIII", 0x48534450, 1, 0xFFFF, app_version, sec_version, 0, 0, 0)
    img[0x200:0x200 + len(hdr)] = hdr
    with open(path, "wb") as f:
        f.write(img)


def sign(tool, key, src, dst):
    env = dict(os.environ, SOURCE_DATE_EPOCH="1700000000")
    subprocess.run([sys.executable, tool, "sign", "--key", key, "--in", src, "--out", dst],
                   check=True, env=env, stdout=subprocess.DEVNULL)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--key", required=True)
    p.add_argument("--tool", required=True)
    p.add_argument("--app-start", required=True, type=lambda s: int(s, 0),
                   help="BLC_APP_START, Makefile lấy từ blc_layout.h")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    o = lambda n: os.path.join(a.out, n)

    # image_size % 8 == 0 (flag cách image_crc 4 byte) và % 8 == 4 (flag liền sau)
    make_bin(o("v1.bin"), 20000, 0x01000000, 0, 1, a.app_start)       # 20000 % 8 == 0
    make_bin(o("v2.bin"), 9 * 1024 + 2, 0x02000000, 2, 2, a.app_start)  # đệm lên 9220, % 8 == 4
    make_bin(o("v3.bin"), 4000, 0x03000000, 1, 3, a.app_start)        # security_version 1 < 2
    for n in ("v1", "v2", "v3"):
        sign(a.tool, a.key, o(n + ".bin"), o(n + ".blcimg"))

    # CRC đúng nhưng chữ ký bởi khóa khác
    other = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    with tempfile.NamedTemporaryFile("wb", suffix=".pem", delete=False) as f:
        f.write(other.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                    serialization.NoEncryption()))
        other_path = f.name
    sign(a.tool, other_path, o("v1.bin"), o("wrongkey.blcimg"))
    os.unlink(other_path)

    # Hỏng 1 byte trong payload -> image_crc sai
    data = bytearray(open(o("v1.blcimg"), "rb").read())
    data[0x1000] ^= 0x01
    open(o("badcrc.blcimg"), "wb").write(data)


if __name__ == "__main__":
    main()
