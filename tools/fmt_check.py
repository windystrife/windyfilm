#!/usr/bin/env python3
"""fmt_check.py - WindyFilm printf format check (WindyStrife's own tool). build.sh runs it on build/<cam>/windyflm.mo;
any finding fails the build.

usage: python3 -I -B tools/fmt_check.py build/<cam>/windyflm.mo

The module formats with Canon's DryOS vsnprintf (ML's snprintf passes through to it). It has no %u, no length
modifiers (%lu, %zu, %lld, ...), no floats and no '+' flag, and prints %c wrong (ML's own notes); the first camera log
of 11-14 showed "u.u" for every "%5u.%u" time stamp. So every '%' in a C string of the module (the string literal
sections .rodata.str*, split at NUL bytes) must be one of:
    %d %x %X %s    with only the flags '-' / '0' and a width (digits), or %%
A '%' that ends the string right after a digit is a label, not a conversion ("100%", the Strength choices, shown
through "%s"). A '%' that starts anything else is a finding: a format that would print garbage, or a plain text with
a '%' that a format call would misread (write "pct" or move the text out of the format string)."""
import re, struct, sys

ALLOWED = re.compile(rb"%%|%[-0]*[0-9]*[dxXs]")
SPEC = re.compile(rb"%[-+ #0]*(?:[0-9]+|\*)?(?:\.(?:[0-9]+|\*))?(?:hh|ll|[hlzjtLq])?.?")


def str_sections(data):
    """the string literal sections (.rodata.str*) of an ELF32 little-endian file: [(name, bytes)]"""
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit("fmt_check: not an ELF32 LE file")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    hdrs = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    stro = hdrs[shstrndx][4]
    res = []
    for h in hdrs:
        name_off, typ, _, _, off, size = h[:6]
        name = data[stro + name_off:data.index(b"\0", stro + name_off)].decode("ascii", "replace")
        if typ == 1 and size and name.startswith(".rodata.str"):
            res.append((name, data[off:off + size]))
    return res


def runs(data):
    """the C strings (NUL-terminated pieces) of the string sections that hold a '%'"""
    for _, sec in str_sections(data):
        for s in sec.split(b"\0"):
            if b"%" in s:
                yield s


def findings(data):
    out = []
    for s in runs(data):
        i = 0
        while True:
            i = s.find(b"%", i)
            if i < 0:
                break
            if i == len(s) - 1 and i > 0 and s[i - 1:i].isdigit():
                break                                # a label ending in "<digit>%"
            m = ALLOWED.match(s, i)
            if m:
                i = m.end()
                continue
            bad = SPEC.match(s, i).group(0)
            out.append((s, bad))
            i += max(1, len(bad))
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    if not str_sections(data):
        sys.exit("fmt_check: no .rodata.str* section in %s" % sys.argv[1])
    bad = findings(data)
    for s, spec in bad:
        print("fmt_check: %r in %r: not a DryOS vsnprintf conversion (only %%d %%x %%X %%s, flags - 0, a width)"
              % (spec.decode("ascii", "replace"), s.decode("ascii", "replace")))
    n = sum(1 for _ in runs(data))
    if bad:
        print("fmt_check: %d finding(s) in %s" % (len(bad), sys.argv[1]))
        sys.exit(1)
    print("fmt_check: %d strings with '%%' in %s, all DryOS-safe (%%d %%x %%X %%s, flags - 0, width)" % (n, sys.argv[1]))


if __name__ == "__main__":
    main()
