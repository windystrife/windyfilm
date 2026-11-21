#!/usr/bin/env python3
"""provenance_gate.py - WindyFilm provenance gate (docs/DESIGN.md 1 and 6), WindyStrife's own tool. build.sh runs it;
any finding fails the build.

usage: python3 -I -B tools/provenance_gate.py --mo build/<cam>/windyflm.mo [--packs DIR] [--core-sym CARD.sym ...]
                                              [--allow tools/gate_allow.txt] [--der ROOT] [--repo ROOT] [--author MO]

Checks:
  1. strings: no printable ASCII string of >= 6 chars from the third-party module appears in our module or in the card
     packs (DIR/*.DAT), except the generic ones: lines "str <text>" of the allowlist, ML / DryOS core symbol names
     (--core-sym: the camera's card .sym; a name the core exports is ML's API, not the author's), and runs of one
     repeated character. A "deny-ok" phrase (below) is removed from our files first.
  2. windows: no 32-byte window of our module's code / data sections (allocated PROGBITS) equals one in the third-party
     module's, ignoring runs of a single byte: windows in which one byte value fills >= 28 of the 32 bytes (padding,
     zero literals of relocated words, zero-filled tables with a stray value) carry no code or data of anyone.
  3. deny words in our sources (src/, tools/, films/, cans/, tests/, build.sh, test.sh), our module and the packs:
     the third-party author's name and data / module names, the derived (DER) file names and the Genesis file names.
     Case-blind. A phrase listed as "deny-ok <text>" in the allowlist is removed first (exact case): the coexistence
     status text docs/DESIGN.md 3 prescribes and the card folder names of the .sym paths. docs/ is not scanned.
  4. byte-equal: no file under src/, tools/, films/, cans/ is byte-equal to a DER file of the phase 1 tree (--der,
     default /root/ml/wf_src; skipped with a note when that tree is not there).
  5. card directory calls: FIO_CreateFile, FIO_CreateFileOrAppend, FIO_RemoveFile and FIO_RenameFile appear in src/
     (comments and strings aside) only in src/wf_main.c: in its extern lines and inside wf_fremove / wf_fcreate /
     wf_frename (src/wf_fio.h: Canon makes a second entry of the same name when a file is created over one; those
     three wait for an idle camera and check the name is gone first).
Public standard tables: the ITU-T T.81 (JPEG) Annex K tables every baseline JPEG encoder carries (the zig-zag order of
Figure A.6 and its inverse, the example quantisation tables K.1 / K.2 in natural and zig-zag order, the Huffman tables
K.3-K.6: code-length counts and values) are built here from the standard and masked (set to 0) in both modules before
checks 1 and 2, as 8-, 16- and 32-bit little-endian arrays, exact whole-table runs only. A shared standard table is then
no finding, while the bytes around it are still compared. The summary line counts the masked runs.
The third-party module is read only here, as deny input; its strings are extracted at run time and never stored.
The deny words are assembled from pieces so this file and the allowlist (both skipped by check 3) do not hold them."""
import hashlib, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
AUTHOR_MO = os.path.join("/mnt/f/MagicLatern/6D/ML/modules", "film" + "roll" + ".mo")
DER_ROOT = "/root/ml/wf_src"
DENY = ["florin" + "kiritescu", "FILM" + "CAN", "FILM" + "ROLL", "film" + "roll",
        # DER file names (phase 1 audit: derived from the author's binary or from Genesis)
        "fr_" + "kern.c", "fr_" + "opt110", "fr_" + "authprev", "fr_" + "5d2.c", "patch" + "_mo.py",
        "audit" + "_mo.py", "proto/" + "speed",
        # Genesis file names
        "Genesis" + "_Pro", "genesis" + "_re", "HANDOFF-" + "GENESIS"]

# Declared exception: branch `film-stage` only (DESIGN 1a, owner 14 Nov 2026). The Genesis-derived table *generator*
# lives in tools/ there, so its name is allowed on that branch and nowhere else: denied by default, taken out of the
# list on the declared branch. The gate then says out loud that the tree is not clean-room, so a PASS there can never
# be read as a clean tree.
BRANCH_EXCEPTIONS = ("fr_" + "glook", "fr_" + "gx", "mk_" + "print_lut.py")


def branch():
    """the checked-out branch name, or '' when this is not a git tree (a `git archive` build).
    In a worktree `.git` is a *file* holding `gitdir: <path>`, so that is followed first."""
    g = os.path.join(REPO, ".git")
    try:
        if os.path.isfile(g):
            for ln in open(g, encoding="utf-8"):
                if ln.startswith("gitdir:"):
                    g = ln.split(":", 1)[1].strip()
                    break
            else:
                return ""
            if re.match(r"[A-Za-z]:[/\\]", g) and not os.path.isdir(g):   # a worktree made from Windows, read in WSL
                g = "/mnt/" + g[0].lower() + g[2:].replace("\\", "/")
            g = os.path.join(REPO, g)                    # a relative gitdir is relative to the worktree
        head = open(os.path.join(g, "HEAD"), encoding="utf-8").read().strip()
    except OSError:
        return ""
    if not head.startswith("ref:"):
        return ""                                    # detached HEAD: no branch to name
    return head[4:].strip().rsplit("/", 1)[-1]


BRANCH = branch()
# `grain-halation` is cut from `film-stage` and carries the same declared exception plus the plugin's *code*
# constants (the grain amplitude tables and the halation formula, DESIGN 1a "what this branch adds on top"):
# it is not clean-room either, and it must not be published or merged into `main` without that paragraph.
# `print-frame` took `film-stage` in on 15 Nov 2026 (the owner: merge it in when it is done), so it carries the same
# exception from that merge on; its clean-room print work ends at 47f9597, the commit before the merge.
# `darkroom` took `print-frame` (with film-stage 4f92b21) in on 15 Nov 2026 (the owner: "merge fillm stage đi bạn"),
# so it carries the same exception from that merge on; its clean-room work ends at fe4190e, the commit before it.
# `chain-log` (option (c): chain films on the RAW develop from a log pack) is cut from darkroom dcc14dc and took
# film-stage c0c9fea in on 15 Nov 2026 (its log-table generator): the same exception from its first commit on.
# `main-staging` took darkroom c111cd9 and chain-log 8ddc4b4 in on 15 Nov 2026 (the owner: "merge thử bản mới nhất
# film-stage vừa làm vào main-stagging có darkroom"), so it carries the same exception from 08fa8a7 on; its clean-room
# state ends at 9775ddd, the commit before. `main` stays clean-room until the owner decides otherwise.
# `dark-nomem` (the darkroom's frame in pieces when the EOS M has one SRM chunk) and `paper-finish` (the Paper row,
# tools/mk_paper.py; the paper model is our own: physical paper behaviour and public data sheets) are cut from
# main-staging 3b45114, so they carry the same exception from their first commit on; `rawb20` merges both.
# `dr-logo` (the print's logo from the owner's card-time pack, Camera JPEG Overwrite) is cut from rawb20 6cdcb76: the
# same exception from its first commit on. `dk-screen` (the Darkroom's Screen Off) is cut from dr-logo bdaed37:
# the same. `wf-onescreen` (the browser's mode chip, one screen) is cut from dk-screen d0c2545: the same.
# `dark-5d2` (the Darkroom on the 5D Mark II) is cut from wf-onescreen cedbfb0: the same.
# `modernUI` (the film OS, docs/MODERNUI.md) is cut from wf-onescreen 6e7f700: the same.
# `labfx` (the Lab effects with the film-lab numbers, DESIGN 3.20) is cut from wf-onescreen 6e7f700 (RAWB28): the same.
NOT_CLEAN = BRANCH in ("film-stage", "grain-halation", "print-frame", "darkroom", "chain-log", "main-staging",
                       "dark-nomem", "paper-finish", "rawb20", "dr-logo", "dk-screen", "wf-onescreen", "dark-5d2",
                       "modernUI", "labfx")
DENY += list(BRANCH_EXCEPTIONS)                       # the derived generator's names are denied by default ...
if NOT_CLEAN:                                         # ... and allowed only under the declared exception
    DENY = [w for w in DENY if w not in BRANCH_EXCEPTIONS]
    print("gate: this tree is NOT clean-room (Genesis-derived tables and effect constants, DESIGN 1a, branch %s)" % BRANCH)
SCAN_DIRS = ("src", "tools", "films", "cans", "tests")
SCAN_FILES = ("build.sh", "test.sh")
EQUAL_DIRS = ("src", "tools", "films", "cans")
SELF = {os.path.join("tools", "provenance_gate.py"), os.path.join("tools", "gate_allow.txt")}
MINSTR = 6
FIO_DIR = ("FIO_CreateFile", "FIO_CreateFileOrAppend", "FIO_RemoveFile", "FIO_RenameFile")
FIO_HOME = os.path.join("src", "wf_main.c")
FIO_FUNCS = ("wf_fremove", "wf_fcreate", "wf_frename")
WIN = 32
WIN_FILL = 28                       # a window with one byte value >= this many times is a run of a single byte


def zigzag():
    """T.81 Figure A.6: zig-zag index -> natural (row-major) index"""
    return [n for _, _, n in sorted((x + y, y if (x + y) % 2 else x, y * 8 + x) for y in range(8) for x in range(8))]


ZZ = zigzag()
IZZ = [ZZ.index(i) for i in range(64)]
K1 = bytes.fromhex("100b0a101828333d0c0c0e131a3a3c370e0d1018283945380e11161d3357503e"
                   "12162538446d674d182337405168715c31404e5767797865485c5f6270646763")
K2 = bytes.fromhex("1112182f6363636312151a4263636363181a3863636363632f42636363636363" + "63" * 32)
K5V = bytes.fromhex("01020300041105122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728"
                    "292a3435363738393a434445464748494a535455565758595a636465666768696a737475767778797a838485868788"
                    "898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9da"
                    "e1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9fa")
K6V = bytes.fromhex("000102031104052131061241510761711322328108144291a1b1c109233352f0156272d10a162434e125f11718191a"
                    "262728292a35363738393a434445464748494a535455565758595a636465666768696a737475767778797a82838485"
                    "868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7"
                    "d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9fa")
ANNEX_K = [("zig-zag", bytes(ZZ)), ("zig-zag inverse", bytes(IZZ)), ("K.1", K1), ("K.2", K2),
           ("K.1 zig-zag", bytes(K1[z] for z in ZZ)), ("K.2 zig-zag", bytes(K2[z] for z in ZZ)),
           ("K.3 counts", bytes([0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0])),
           ("K.4 counts", bytes([0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0])), ("K.3/K.4 values", bytes(range(12))),
           ("K.5 counts", bytes([0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d])),
           ("K.6 counts", bytes([0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77])), ("K.5 values", K5V),
           ("K.6 values", K6V)]
assert len(K1) == len(K2) == 64 and len(K5V) == len(K6V) == 162 and ZZ[:6] == [0, 1, 8, 16, 9, 2] and ZZ[-1] == 63


def mask_public(data, count):
    """data with every whole Annex K table run (8 / 16 / 32-bit LE) set to 0; count[name] += runs masked"""
    b = bytearray(data)
    for name, tab in ANNEX_K:
        for w in (1, 2, 4):
            pat = b"".join(v.to_bytes(w, "little") for v in tab)
            o = b.find(pat)
            while o >= 0:
                b[o:o + len(pat)] = bytes(len(pat))
                count[name] = count.get(name, 0) + 1
                o = b.find(pat, o + len(pat))
    return bytes(b)


def strings(data, n=MINSTR):
    out, cur = set(), bytearray()
    for b in data:
        if 32 <= b < 127:
            cur.append(b)
            continue
        if len(cur) >= n:
            out.add(bytes(cur))
        cur.clear()
    if len(cur) >= n:
        out.add(bytes(cur))
    return out


def sections(data):
    """allocated PROGBITS sections of an ELF32 little-endian file: [(name, bytes)]"""
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit("gate: not an ELF32 LE file")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    hdrs = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    stro = hdrs[shstrndx][4]
    res = []
    for h in hdrs:
        name_off, typ, flags, _, off, size = h[:6]
        if typ == 1 and flags & 2 and size:
            e = data.index(b"\0", stro + name_off)
            res.append((data[stro + name_off:e].decode("ascii", "replace"), data[off:off + size]))
    return res


def one_byte(w):
    return w.count(w[0]) == len(w)


def low_info(w):
    return max(w.count(b) for b in set(w)) >= WIN_FILL


def load_allow(path):
    strs, deny_ok = set(), []
    if not os.path.isfile(path):
        return strs, deny_ok
    for line in open(path, encoding="ascii"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        kind, _, text = line.partition(" ")
        if kind == "str":
            strs.add(text.encode("ascii"))
        elif kind == "deny-ok":
            deny_ok.append(text.encode("ascii"))
        else:
            sys.exit("gate: %s: unknown line %r" % (path, line))
    return strs, deny_ok


def walk(root, dirs, files):
    for d in dirs:
        for base, subs, names in os.walk(os.path.join(root, d)):
            subs[:] = sorted(s for s in subs if s not in ("__pycache__", ".git"))
            for nm in sorted(names):
                p = os.path.join(base, nm)
                yield os.path.relpath(p, root), p
    for f in files:
        p = os.path.join(root, f)
        if os.path.isfile(p):
            yield f, p


def uncomment(t):
    """C / asm text with comments and string / char literals blanked (newlines kept)"""
    out, i, n = [], 0, len(t)
    blank = lambda s: re.sub(r"[^\n]", " ", s)
    while i < n:
        if t.startswith("/*", i):
            j = t.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(blank(t[i:j]))
        elif t.startswith("//", i):
            j = t.find("\n", i)
            j = n if j < 0 else j
            out.append(blank(t[i:j]))
        elif t[i] in "\"'":
            j = i + 1
            while j < n and t[j] != t[i] and t[j] != "\n":
                j += 2 if t[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(blank(t[i:j]))
        else:
            j = i + 1
            out.append(t[i])
        i = j
    return "".join(out)


def fio_calls(repo):
    """check 5: (allowed, [(rel, line, name)] outside wf_main.c's extern lines and wf_fremove / wf_fcreate / wf_frename)"""
    call = re.compile(r"\b(%s)\b" % "|".join(FIO_DIR))
    head = re.compile(r"^[A-Za-z_][^;{}]*\b(%s)\s*\([^;{}]*\)\s*\{" % "|".join(FIO_FUNCS), re.M)
    close = re.compile(r"^\}", re.M)
    allowed, bad = 0, []
    for rel, path in walk(repo, ("src",), ()):
        if not rel.endswith((".c", ".h", ".S")):
            continue
        t = uncomment(open(path, encoding="utf-8", errors="replace").read())
        spans = []
        if rel == FIO_HOME:
            for m in head.finditer(t):
                e = close.search(t, m.end())
                spans.append((m.start(), e.end() if e else len(t)))
        for m in call.finditer(t):
            bol = t.rfind("\n", 0, m.start()) + 1
            if rel == FIO_HOME and (t.startswith("extern", bol) or any(a <= m.start() < b for a, b in spans)):
                allowed += 1
            else:
                bad.append((rel, t.count("\n", 0, m.start()) + 1, m.group(1)))
    return allowed, bad


def main(argv):
    a = {"--packs": None, "--allow": os.path.join(HERE, "gate_allow.txt"), "--der": DER_ROOT, "--repo": REPO,
         "--author": AUTHOR_MO, "--mo": None}
    core = []
    i = 1
    while i < len(argv):
        k = argv[i]
        if k == "--core-sym":
            core.append(argv[i + 1])
        elif k in a:
            a[k] = argv[i + 1]
        else:
            sys.exit(__doc__)
        i += 2
    if not a["--mo"]:
        sys.exit(__doc__)
    if not os.path.isfile(a["--author"]):
        sys.exit("gate: FAIL: the deny input (the third-party module) is not at the expected place")
    kmask = {}
    author = mask_public(open(a["--author"], "rb").read(), {})
    ours = mask_public(open(a["--mo"], "rb").read(), kmask)
    packs = []
    if a["--packs"] and os.path.isdir(a["--packs"]):
        packs = [os.path.join(a["--packs"], n) for n in sorted(os.listdir(a["--packs"])) if n.upper().endswith(".DAT")]
    allow, deny_ok = load_allow(a["--allow"])
    coresyms = set()
    for c in core:
        for line in open(c, encoding="ascii", errors="replace"):
            p = line.split()
            if len(p) >= 2:
                coresyms.add(p[1].encode("ascii", "replace"))
    fails = []

    # 1. strings
    astr = strings(author)
    generic = {s for s in astr if s in allow or s in coresyms or one_byte(s)}
    targets = [(a["--mo"], ours)] + [(p, mask_public(open(p, "rb").read(), kmask)) for p in packs]
    for k, (path, data) in enumerate(targets):
        for ok in deny_ok:
            data = data.replace(ok, b"")
        targets[k] = (path, data)
    nstr = 0
    for path, data in targets:
        for s in sorted(astr - generic):
            if s in data:
                nstr += 1
                fails.append("string %r of the third-party module is in %s" % (s.decode("ascii"), os.path.basename(path)))
    # 2. 32-byte windows of code / data
    aw = set()
    for _, d in sections(author):
        for o in range(len(d) - WIN + 1):
            aw.add(d[o:o + WIN])
    nwin = nlow = 0
    for name, d in sections(ours):
        for o in range(len(d) - WIN + 1):
            w = d[o:o + WIN]
            if w not in aw:
                continue
            if low_info(w):
                nlow += 1
            else:
                nwin += 1
                if nwin <= 10:
                    fails.append("32-byte window at %s+0x%x equals one in the third-party module (%s)" % (name, o, w.hex()))
    if nwin > 10:
        fails.append("... %d equal windows in all" % nwin)
    # 3. deny words
    deny = [w.lower().encode("ascii") for w in DENY]
    nfiles = 0
    scan = list(walk(a["--repo"], SCAN_DIRS, SCAN_FILES)) + [(os.path.basename(p), p) for p, _ in targets]
    for rel, path in scan:
        if rel in SELF or os.path.getsize(path) > 64 << 20:
            continue
        data = open(path, "rb").read()
        for ok in deny_ok:
            data = data.replace(ok, b"")
        data = data.lower()
        nfiles += 1
        for w in deny:
            if w in data:
                fails.append("deny word %r in %s" % (w.decode("ascii"), rel))
    # 4. byte-equal to a DER file
    der = {}
    if os.path.isdir(a["--der"]):
        pre = ("fr_" + "kern.c", "fr_" + "opt110.S", "fr_" + "authprev.c", "fr_" + "authprev.h", "fr_" + "5d2.c",
               "patch" + "_mo.py", "audit" + "_mo.py")
        for base, subs, names in os.walk(a["--der"]):
            subs[:] = [s for s in subs if s != ".git"]
            in_speed = os.sep + "proto" + os.sep + "speed" in base + os.sep
            for nm in names:
                if nm in pre or nm.startswith("fr_" + "glook") or nm.startswith("fr_" + "gx") or in_speed:
                    p = os.path.join(base, nm)
                    der[hashlib.sha256(open(p, "rb").read()).hexdigest()] = os.path.relpath(p, a["--der"])
        nequal = 0
        for rel, path in walk(a["--repo"], EQUAL_DIRS, ()):
            h = hashlib.sha256(open(path, "rb").read()).hexdigest()
            if h in der:
                nequal += 1
                fails.append("%s is byte-equal to a DER file (%s)" % (rel, der[h]))
        der_note = "%d DER files hashed, 0 equal" % len(der) if not nequal else "%d equal" % nequal
    else:
        der_note = "DER tree not found: skipped"
    # 5. card directory calls only in wf_main.c's wf_fremove / wf_fcreate / wf_frename
    fio_ok, fio_bad = fio_calls(a["--repo"])
    for rel, ln, nm in fio_bad:
        fails.append("%s:%d calls %s: card files are made / removed / renamed only through wf_fcreate / wf_fremove / "
                     "wf_frename (src/wf_fio.h)" % (rel, ln, nm))
    for f in fails[:int(os.environ.get("WF_GATE_SHOW", "40"))]:
        print("gate: FAIL: " + f)
    if kmask:
        print("gate: public T.81 Annex K tables masked in ours: " + ", ".join("%s x%d" % kv for kv in sorted(kmask.items())))
    print("gate: %s: strings %d checked (%d generic: %d allowlist, the rest core names / one-char runs) -> %d found; "
          "windows -> %d equal (%d runs of a single byte ignored); deny words: %d files; byte-equal: %s; "
          "card directory calls: %d in wf_main.c's wf_fio.h functions / externs, %d elsewhere"
          % ("PASS" if not fails else "FAIL", len(astr), len(generic), len(allow & astr), nstr, nwin, nlow, nfiles,
             der_note, fio_ok, len(fio_bad)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
