#!/usr/bin/env python3
"""jpg_pictures.py - every picture inside a camera JPEG or CR2 (WindyStrife's own tool, owner 2026-11-17: after Camera
JPEG Overwrite the EOS M's playback still showed Canon's colours, even after a power cycle). Read-only.

usage: python3 -I -B tools/jpg_pictures.py IMG_0501.JPG IMG_0501.CR2 [...]

For each file one line per picture: where it sits (JPEG: the main image, Exif IFD1's thumbnail; CR2: IFD0's full-size
JPEG, IFD1's thumbnail, IFD2's uncompressed RGB, IFD3's raw data), offset, bytes, size, chroma sampling and the first
8 hex digits of its md5. The same md5 in a JPEG and in its CR2 means the picture is Canon's (a fresh RAW+L pair's two
thumbnails are byte for byte the same; after Overwrite the JPEG's must differ). Then the places a further preview could
hide: an MPF APP2 (multi-picture), a Canon MakerNote preview tag (0x00B6), an image start (FF D8 FF) the walk did not
name, bytes after the last EOI. A real EOS M pair (IMG_6851, firmware 2.0.2) has none of them: the JPEG holds the main
image and the 160 x 120 thumbnail, nothing else; the CR2 holds four (IFD0 5184 x 3456 JPEG, IFD1 160 x 120 JPEG,
IFD2 660 x 441 RGB, IFD3 raw)."""
import hashlib
import struct
import sys


def be16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def md5(b):
    return hashlib.md5(b).hexdigest()[:8]


def jpeg_info(b, o, n):
    """(w, h, sampling) of the JPEG stream at b[o:o+n] from its SOF, or None"""
    i, e = o + 2, o + n
    while i + 4 <= e and b[i] == 0xFF:
        m, ln = b[i + 1], be16(b, i + 2)
        if m in (0xC0, 0xC1, 0xC2):
            h, w, nc = be16(b, i + 5), be16(b, i + 7), b[i + 9]
            smp = "".join("%dx%d" % (b[i + 11 + 3 * k] >> 4, b[i + 11 + 3 * k] & 15) + (" " if k < nc - 1 else "")
                          for k in range(nc))
            return w, h, smp
        if m == 0xDA:
            return None
        i += 2 + ln
    return None


def jpeg_end(b, o):
    """the offset after the EOI of the JPEG stream starting at o (segments, then the scan), or len(b)"""
    i = o + 2
    while i + 4 <= len(b) and b[i] == 0xFF and b[i + 1] != 0xDA:
        i += 2 + be16(b, i + 2)
    e = b.find(b"\xff\xd9", i)
    return len(b) if e < 0 else e + 2


class Tiff:
    def __init__(self, b, base):
        self.b, self.base = b, base
        self.le = b[base:base + 2] == b"II"
        self.f = "<" if self.le else ">"

    def u16(self, o):
        return struct.unpack_from(self.f + "H", self.b, self.base + o)[0]

    def u32(self, o):
        return struct.unpack_from(self.f + "I", self.b, self.base + o)[0]

    def ifd(self, o):
        """{tag: (type, count, value or offset)}, next IFD"""
        n = self.u16(o)
        d = {}
        for k in range(n):
            e = o + 2 + 12 * k
            typ, cnt = self.u16(e + 2), self.u32(e + 4)
            v = self.u16(e + 8) if typ == 3 and cnt == 1 else self.u32(e + 8)
            d[self.u16(e)] = (typ, cnt, v)
        return d, self.u32(o + 2 + 12 * n)

    def chain(self, limit=6):
        out, o = [], self.u32(4)
        while o and len(out) < limit and self.base + o + 2 <= len(self.b):
            d, o = self.ifd(o)
            out.append(d)
        return out

    def maker_tags(self, ifd0):
        if 0x8769 not in ifd0:
            return {}
        ex, _ = self.ifd(ifd0[0x8769][2])
        if 0x927C not in ex:
            return {}
        mk, _ = self.ifd(ex[0x927C][2])
        return mk


def say(name, b, o, n, raw=None):
    if raw:
        print("  %-22s off %9d  %9d B  %s  md5 %s" % (name, o, n, raw, md5(b[o:o + n])))
        return
    inf = jpeg_info(b, o, n) if b[o:o + 2] == b"\xff\xd8" else None
    dims = "%d x %d  %s" % inf if inf else "(not a JPEG)"
    print("  %-22s off %9d  %9d B  %s  md5 %s" % (name, o, n, dims, md5(b[o:o + n])))


def starts(b):
    out, p = [], 0
    while True:
        p = b.find(b"\xff\xd8\xff", p)
        if p < 0:
            return out
        out.append(p)
        p += 3


def one_jpeg(path, b):
    print("%s: JPEG, %d B" % (path, len(b)))
    named, notes = [0], []
    i = 2
    while i + 4 <= len(b) and b[i] == 0xFF and b[i + 1] != 0xDA:
        m, ln = b[i + 1], be16(b, i + 2)
        tag = b[i + 4:i + 10]
        if m == 0xE1 and tag.startswith(b"Exif\0"):
            t = Tiff(b, i + 10)
            ch = t.chain()
            if len(ch) > 1 and 0x0201 in ch[1] and 0x0202 in ch[1]:
                o, n = i + 10 + ch[1][0x0201][2], ch[1][0x0202][2]
                say("Exif IFD1 thumbnail", b, o, n)
                named.append(o)
            if ch and 0x00B6 in t.maker_tags(ch[0]):
                notes.append("Canon MakerNote preview tag 0x00B6")
        elif m == 0xE2 and tag.startswith(b"MPF"):
            notes.append("MPF APP2 at %d (multi-picture: a second image)" % i)
        elif m == 0xE1:
            notes.append("APP1 %r at %d, %d B (no picture)" % (bytes(tag[:4]), i, ln))
        i += 2 + ln
    end = jpeg_end(b, 0)
    say("main image", b, 0, end)
    for s in starts(b):
        if s not in named:
            notes.append("an image start at %d not named above" % s)
    if end < len(b):
        notes.append("%d B after the main image's EOI" % (len(b) - end))
    for s in notes:
        print("  note: " + s)


def one_cr2(path, b):
    print("%s: TIFF / CR2, %d B" % (path, len(b)))
    t = Tiff(b, 0)
    for k, d in enumerate(t.chain()):
        if 0x0111 in d and 0x0117 in d:
            o, n = d[0x0111][2], d[0x0117][2]
            if d.get(0x0103, (0, 0, 0))[2] == 1:
                say("IFD%d RGB" % k, b, o, n, "%d x %d  uncompressed" % (d[0x0100][2], d[0x0101][2]))
            elif k == 3 or 0xC640 in d:
                say("IFD%d raw data" % k, b, o, n, "lossless (sensor data)")
            else:
                say("IFD%d strip" % k, b, o, n)
        if 0x0201 in d and 0x0202 in d:
            say("IFD%d thumbnail" % k, b, d[0x0201][2], d[0x0202][2])


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    for p in argv:
        b = open(p, "rb").read()
        if b[:2] == b"\xff\xd8":
            one_jpeg(p, b)
        elif b[:2] in (b"II", b"MM"):
            one_cr2(p, b)
        else:
            print("%s: neither a JPEG nor a TIFF / CR2" % p)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
