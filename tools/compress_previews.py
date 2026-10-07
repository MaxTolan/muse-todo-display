#!/usr/bin/env python3
"""Recompress simulator screenshots (stored, uncompressed PNGs) with zlib so
they're small enough to commit, and write a contact sheet of all of them.

    compress_previews.py SRC_DIR DST_DIR
"""

import pathlib
import struct
import sys
import zlib


def read_png(path):
    data = path.read_bytes()
    i, idat, w, h = 8, b"", 0, 0
    while i < len(data):
        n = struct.unpack(">I", data[i : i + 4])[0]
        kind, body = data[i + 4 : i + 8], data[i + 8 : i + 8 + n]
        if kind == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        i += 12 + n
    raw = zlib.decompress(idat)
    row = w * 3 + 1
    return w, h, [raw[y * row + 1 : (y + 1) * row] for y in range(h)]


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    raw = b"".join(b"\0" + r for r in rows)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def half(w, h, rows):
    """2x box downscale for the contact sheet."""
    out = []
    for y in range(0, h - 1, 2):
        a, b = rows[y], rows[y + 1]
        line = bytearray()
        for x in range(0, w - 1, 2):
            for c in range(3):
                i, j = x * 3 + c, (x + 1) * 3 + c
                line.append((a[i] + a[j] + b[i] + b[j]) // 4)
        out.append(bytes(line))
    return w // 2, h // 2, out


def main():
    src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    for old in dst.glob("*.png"):
        old.unlink()
    shots = sorted(src.glob("*.png"))
    thumbs = []
    for shot in shots:
        w, h, rows = read_png(shot)
        write_png(dst / shot.name, w, h, rows)
        thumbs.append(half(w, h, rows))

    cols, gap = 3, 8
    tw, th = thumbs[0][0], thumbs[0][1]
    nrows = (len(thumbs) + cols - 1) // cols
    sw, sh = cols * tw + (cols + 1) * gap, nrows * th + (nrows + 1) * gap
    bg = bytes([18, 12, 20])
    sheet = [bytearray(bg * sw) for _ in range(sh)]
    for k, (_, _, rows) in enumerate(thumbs):
        ox = gap + (k % cols) * (tw + gap)
        oy = gap + (k // cols) * (th + gap)
        for y, r in enumerate(rows):
            sheet[oy + y][ox * 3 : ox * 3 + len(r)] = r
    write_png(dst / "contact-sheet.png", sw, sh, [bytes(r) for r in sheet])
    print(f"{len(shots)} previews + contact sheet -> {dst}")


if __name__ == "__main__":
    main()
