#!/usr/bin/env python3
"""Convert a 32-bit BMP (as written by --screenshot) to PNG using only the stdlib."""
import struct
import sys
import zlib


def main(src, dst):
    data = open(src, "rb").read()
    if data[:2] != b"BM":
        raise SystemExit("not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    hdr_size = struct.unpack_from("<I", data, 14)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit(f"unsupported bpp {bpp}")
    bottom_up = h > 0
    h = abs(h)
    stride = ((w * bpp // 8) + 3) & ~3
    rows = []
    for y in range(h):
        src_y = (h - 1 - y) if bottom_up else y
        row = data[off + src_y * stride: off + src_y * stride + w * bpp // 8]
        out = bytearray()
        step = bpp // 8
        for x in range(0, len(row), step):
            b, g, r = row[x], row[x + 1], row[x + 2]
            out += bytes((r, g, b))
        rows.append(b"\x00" + bytes(out))
    raw = b"".join(rows)

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    open(dst, "wb").write(png)
    print(f"{dst}: {w}x{h}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
