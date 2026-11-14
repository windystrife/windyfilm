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
        head = open(os.path.join(g, "HEAD"), encoding="utf-8").read().strip()
    except OSError:
        return ""
    if not head.startswith("ref:"):
        return ""                                    # detached HEAD: no branch to name
    return head[4:].strip().rsplit("/", 1)[-1]


BRANCH = branch()
NOT_CLEAN = BRANCH == "film-stage"
DENY += list(BRANCH_EXCEPTIONS)                       # the derived generator's names are denied by default ...
if NOT_CLEAN:                                         # ... and allowed only under the declared exception
    DENY = [w for w in DENY if w not in BRANCH_EXCEPTIONS]
    print("gate: this tree is NOT clean-room (Genesis-derived tables, DESIGN 1a, branch %s)" % BRANCH)
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
    author = open(a["--author"], "rb").read()
    ours = open(a["--mo"], "rb").read()
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
    targets = [(a["--mo"], ours)] + [(p, open(p, "rb").read()) for p in packs]
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
    print("gate: %s: strings %d checked (%d generic: %d allowlist, the rest core names / one-char runs) -> %d found; "
          "windows -> %d equal (%d runs of a single byte ignored); deny words: %d files; byte-equal: %s; "
          "card directory calls: %d in wf_main.c's wf_fio.h functions / externs, %d elsewhere"
          % ("PASS" if not fails else "FAIL", len(astr), len(generic), len(allow & astr), nstr, nwin, nlow, nfiles,
             der_note, fio_ok, len(fio_bad)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
