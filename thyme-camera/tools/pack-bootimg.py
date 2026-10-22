#!/usr/bin/env python3
"""Pack a header-v1 Android boot image for thyme (Mi 10S) mainline testing.

Replicates the layout found in the device's boot_a slot (the alpha-1.0
delivery): header v1, pagesize 2048, kernel = Image.gz with the DTB
appended after the gzip stream (qcom ABL scans the appended blob and
picks the DTB whose qcom,msm-id/board-id match), no ramdisk.

Usage: pack-bootimg.py <Image.gz> <dtb> <out.img> [cmdline]
"""
import struct
import sys

PAGE = 2048
KERNEL_ADDR = 0x10000000
TAGS_ADDR = 0x10000000

def pad(f):
    off = f.tell() % PAGE
    if off:
        f.write(b"\0" * (PAGE - off))

def main():
    image_gz, dtb, out = sys.argv[1], sys.argv[2], sys.argv[3]
    cmdline = sys.argv[4].encode() if len(sys.argv) > 4 else b""
    assert len(cmdline) <= 512, "cmdline too long for the 512-byte field"

    kernel = open(image_gz, "rb").read() + open(dtb, "rb").read()

    hdr = struct.pack(
        "<8s10I16s512s32s1024sIQ",
        b"ANDROID!",
        len(kernel), KERNEL_ADDR,     # kernel
        0, 0,                         # ramdisk
        0, 0,                         # second
        TAGS_ADDR, PAGE,
        1,                            # header version
        0,                            # os_version
        b"",                          # name
        cmdline,
        b"\0" * 32,                   # id/sha (unused by ABL for fastboot boot)
        b"",                          # extra cmdline
        0, 0,                         # recovery_dtbo size/offset (v1)
    )
    # v1 header tail: header_size field
    hdr += struct.pack("<I", 1648)

    with open(out, "wb") as f:
        f.write(hdr)
        pad(f)
        f.write(kernel)
        pad(f)

    print(f"packed {out}: kernel(+dtb)={len(kernel)} bytes, cmdline={cmdline!r}")

if __name__ == "__main__":
    main()
