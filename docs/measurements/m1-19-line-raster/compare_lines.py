"""SPIKE (M1-19, not for commit): compare two probe PNGs pixel by pixel.

Usage: python compare_lines.py A.png B.png
Reads 8-bit RGB PNGs with the standard library only. Before comparing, it
proves it can see a difference: it compares A with A shifted one pixel to the
right and refuses to go on unless that shows differences.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and colour == 2, "only 8-bit RGB"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw, bpp, stride = zlib.decompress(idat), 3, width * 3
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return width, height, rows


def compare(a, b):
    (w, h, ra), (_, _, rb) = a, b
    lit_a = lit_b = both = differ = largest = 0
    for y in range(h):
        for x in range(w):
            pa, pb = ra[y][3 * x:3 * x + 3], rb[y][3 * x:3 * x + 3]
            la, lb = any(pa), any(pb)
            lit_a += la; lit_b += lb; both += la and lb
            if pa != pb:
                differ += 1
                largest = max(largest, max(abs(p - q) for p, q in zip(pa, pb)))
    return lit_a, lit_b, both, differ, largest


def shifted(img):
    w, h, rows = img
    return w, h, [b"\0\0\0" + r[:-3] for r in rows]


def main():
    a, b = read_png(sys.argv[1]), read_png(sys.argv[2])
    if a[:2] != b[:2]:
        sys.exit(f"sizes differ: {a[:2]} and {b[:2]}")
    control = compare(a, shifted(a))
    if control[3] == 0:
        sys.exit("control failed: a one-pixel shift showed no difference")
    print(f"control (A against A shifted one pixel): {control[3]} pixels differ -- the check can see")
    lit_a, lit_b, both, differ, largest = compare(a, b)
    print(f"lit pixels: A {lit_a}, B {lit_b}, lit in both {both}")
    print(f"pixels that differ: {differ}; largest difference in one channel: {largest}/255")


main()
