#!/usr/bin/env python3
"""mk_devmat.py [--check FILE] - camera matrices of the RAW develop (src/wf_dev.c, WindyStrife).

dcraw's method from the Adobe / dcraw ColorMatrix (XYZ D65 -> camera, x 10000) of each camera:
  cam_rgb = cam_xyz . xyz_rgb (sRGB / Rec.709 primaries, D65), each row normalised to sum 1 (white balanced camera
            space: a neutral has equal channels)
  rgb_cam = inverse(cam_rgb)  (camera -> linear Rec.709; rows sum to 1)
  daylight multipliers (D65) = 1 / row sums of the unnormalised cam_rgb, G = 1.
Prints the C arrays wf_dev.c holds (Q20, each row's rounding put on the diagonal so rows sum exactly to 2^20) and, with
--check FILE, verifies that FILE (src/wf_dev.c) contains exactly these numbers. Pure stdlib, deterministic.
"""
import re
import sys

CAMS = [
    ("EOS M", [6602, -841, -939, -4472, 12458, 2247, -975, 2039, 6148]),
    ("5D Mark II", [4716, 603, -830, -7798, 15474, 2480, -1496, 1937, 6651]),
]
XYZ_RGB = [[0.412453, 0.357580, 0.180423], [0.212671, 0.715160, 0.072169], [0.019334, 0.119193, 0.950227]]


def mul(A, B):
    return [[sum(A[r][k] * B[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def inverse(m):
    a, b, c = m[0]; d, e, f = m[1]; g, h, i = m[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
            [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
            [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]


def q20(M):
    out = []
    for r in range(3):
        row = [int(round(M[r][c] * (1 << 20))) for c in range(3)]
        row[r] += (1 << 20) - sum(row)
        out += row
    return out


def tables():
    rgb_cam, cam_rgb, pre = [], [], []
    for name, cx in CAMS:
        cam_xyz = [[cx[3 * r + c] / 10000.0 for c in range(3)] for r in range(3)]
        cr = mul(cam_xyz, XYZ_RGB)
        sums = [sum(row) for row in cr]
        crn = [[v / s for v in row] for row, s in zip(cr, sums)]
        rgb_cam.append(q20(inverse(crn)))
        cam_rgb.append(q20(crn))
        p = [1.0 / s for s in sums]
        pre.append([int(round(v / p[1] * 4096)) for v in p])
    return rgb_cam, cam_rgb, pre


def main():
    rgb_cam, cam_rgb, pre = tables()
    lines = []
    lines.append("static const int32_t wf_dev_rgbcam[WF_DEV_NCAM][9] = {   /* camera (WB) -> linear Rec.709, Q20 */")
    for (name, _), m in zip(CAMS, rgb_cam):
        lines.append("    { %s },   /* %s */" % (", ".join(str(v) for v in m), name))
    lines.append("};")
    lines.append("static const int32_t wf_dev_camrgb[WF_DEV_NCAM][9] = {   /* linear Rec.709 -> camera (WB), Q20 */")
    for (name, _), m in zip(CAMS, cam_rgb):
        lines.append("    { %s },   /* %s */" % (", ".join(str(v) for v in m), name))
    lines.append("};")
    lines.append("static const int32_t wf_dev_day[WF_DEV_NCAM][3] = {      /* D65 multipliers, G = 4096 */")
    for (name, _), p in zip(CAMS, pre):
        lines.append("    { %s },   /* %s */" % (", ".join(str(v) for v in p), name))
    lines.append("};")
    text = "\n".join(lines)
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        src = open(sys.argv[2]).read()
        nums = lambda s: re.findall(r"-?\d+", s)
        for block in text.split("};"):
            if not block.strip():
                continue
            head = block.strip().splitlines()[0]
            name = re.search(r"(wf_dev_\w+)", head).group(1)
            m = re.search(r"static const int32_t " + name + r"\[[^{]*\{(.*?)\n\};", src, re.S)
            if not m:
                print("check: %s not found in %s" % (name, sys.argv[2]))
                sys.exit(1)
            want = [v for row in block.strip().splitlines()[1:] for v in nums(row.split("/*")[0])]
            got = [v for row in m.group(1).splitlines() for v in nums(row.split("/*")[0])]
            if want != got:
                print("check: %s differs: %s vs %s" % (name, got, want))
                sys.exit(1)
        print("mk_devmat: %s matches (rgb_cam, cam_rgb, daylight multipliers)" % sys.argv[2])
        return
    print(text)


if __name__ == "__main__":
    main()
