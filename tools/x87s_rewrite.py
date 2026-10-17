#!/usr/bin/env python3
"""Mark the operations the original computed at x87 single precision.

Once its Direct3D device exists the game runs with the x87 precision control
at 24 bits (CMR2Decomp/X87Single.h): every add, subtract, multiply, divide and
square root rounds to single precision, while integer operands are loaded
exactly. Float operations of other compilers already round that way; double
ones and integer-to-float conversions do not. This runs a Clang plugin
(tools/raw_access/X87Single.cpp) over CMR2Decomp's sources, parsed with
MSVC6's headers as tools/ftoi_rewrite.py does, and writes those operations as
X87S_ADD/SUB/MUL/DIV(a, b), X87S_SQRT(x), X87S_FILD(float, i) and
X87S_LOAD(i). MSVC6 compiles the macros as the original tokens. Running it again finds nothing
new. Operations it cannot mark in place (compound assignments, macro bodies)
and the math library calls are listed for review.

    tools/x87s_rewrite.py [--decomp ../CMR2Decomp] [--port] [--dry-run] [--skip FILE:LINE ...]

--port marks the port's own code in game/ after a sync (parsed with the port's
headers, where the marks are function calls).

Needs clang-devel and the MSVC6 headers (CMR2_TOOLS/msvc600/VC98/Include).
"""
import argparse
import collections
import concurrent.futures
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ftoi_rewrite  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PLUGIN_SOURCE = ROOT / "tools/raw_access/X87Single.cpp"
PLUGIN = ROOT / "build/tools/X87Single.so"
WORK = ROOT / "build/tools/x87s"


def build_plugin():
    if PLUGIN.exists() and PLUGIN.stat().st_mtime >= PLUGIN_SOURCE.stat().st_mtime:
        return
    PLUGIN.parent.mkdir(parents=True, exist_ok=True)
    flags = [f for f in subprocess.run(["llvm-config", "--cxxflags"], capture_output=True, text=True,
                                       check=True).stdout.split() if not f.startswith("-std=")]
    subprocess.run(["clang++", "-shared", "-fPIC", "-O1", "-std=c++17", "-fno-rtti", *flags, str(PLUGIN_SOURCE),
                    "-o", str(PLUGIN)], check=True)


def run(unit, source, decomp, overlay, shim):
    out = WORK / (Path(unit).stem + ".jsonl")
    if decomp is None:
        # OpenCMR2's game/, with the port's own headers: its PORT code.
        cmd = ["clang++", "-m64", "-fsyntax-only", "-std=gnu++17", "-fms-extensions", "-fsigned-char",
               f"-I{source}", f"-I{ROOT / 'src'}", f"-I{ROOT / 'src/port/compat'}",
               "-include", str(ROOT / "src/port/types.h"), "-include", str(ROOT / "src/port/msvcrt.h"),
               "-Wno-everything", f"-fplugin={PLUGIN}", f"-fplugin-arg-x87s-out={out}",
               f"-fplugin-arg-x87s-dir={source}/", str(source / unit)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        return unit, out, [l for l in result.stderr.splitlines() if ": error:" in l]
    resource = subprocess.run(["clang", "-print-resource-dir"], capture_output=True, text=True).stdout.strip()
    cmd = ["clang++", "-target", "i386-pc-windows-msvc", "-fms-compatibility-version=12.0", "-fms-extensions",
           "-fms-compatibility", "-fsyntax-only", "-nostdinc", "-nostdinc++", "-isystem", f"{resource}/include",
           "-isystem", str(overlay), f"-I{source}", f"-I{decomp / 'third_party/dx7sdk-7001/include'}",
           "-include", str(shim), "-Wno-everything", f"-fplugin={PLUGIN}", f"-fplugin-arg-x87s-out={out}",
           f"-fplugin-arg-x87s-dir={source}/", str(source / unit)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return unit, out, [l for l in result.stderr.splitlines() if ": error:" in l]


def apply(data, rows):
    """Rewrite one file. Edits are insertions at the operands' boundaries and
    replacements of the operator text between them, so nested operations
    compose: at one offset, closing parentheses come first, then the openings
    of the outermost operation."""
    opens = collections.defaultdict(list)    # offset -> [(end, text)]
    closes = collections.Counter()           # offset -> count
    replace = {}                             # start -> (end, text)
    for r in rows:
        if r["t"] == "op":
            opens[r["begin"]].append((r["end"], f"{r['macro']}(".encode()))
            replace[r["lhsEnd"]] = (r["rhsBegin"], b", ")
            closes[r["end"]] += 1
        elif r["t"] == "sqrt":
            replace[r["begin"]] = (r["end"], b"X87S_SQRT")
        elif r["t"] == "fild":
            if "prefix" in r:
                replace[r["prefix"]] = (r["begin"], b"X87S_FILD(float, ")
            else:
                opens[r["begin"]].append((r["end"], b"X87S_LOAD("))
            closes[r["end"]] += 1
    out, i = bytearray(), 0
    for p in sorted(set(opens) | set(closes) | set(replace)):
        if p < i:
            near = [r for r in rows if r["begin"] - 200 < p < r["end"] + 200]
            raise ValueError(f"overlapping edits at offset {p}: {near}")
        out += data[i:p]
        i = p
        out += b")" * closes[p]
        for _end, text in sorted(opens[p], key=lambda o: -o[0]):
            out += text
        if p in replace:
            end, text = replace[p]
            out += text
            i = end
    out += data[i:]
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", type=Path, default=ROOT.parent / "CMR2Decomp")
    ap.add_argument("--msvc", type=Path,
                    default=Path(os.environ.get("CMR2_TOOLS", ROOT.parent / "tools")) / "msvc600/VC98/Include")
    ap.add_argument("--port", action="store_true",
                    help="mark OpenCMR2's game/ instead (port-only lines after a sync)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--skip", nargs="*", default=[],
                    help="FILE:LINE of operations that run before the device exists (at double precision)")
    args = ap.parse_args()
    decomp = args.decomp.resolve()
    build_plugin()
    ftoi_rewrite.WORK = WORK
    WORK.mkdir(parents=True, exist_ok=True)
    if args.port:
        source, decomp, overlay, shim = ROOT / "game", None, None, None
    else:
        source = decomp / "CMR2Decomp"
        overlay, shim = ftoi_rewrite.msvc_overlay(args.msvc), ftoi_rewrite.friend_shim(source)
    units = sorted(p.name for p in source.glob("*.cpp"))
    edits, notes = {}, {}
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        for unit, out, errors in pool.map(lambda u: run(u, source, decomp, overlay, shim), units):
            if errors:
                print(f"{unit}: {len(errors)} parse errors (operations in those statements are missed):",
                      errors[0][:160], file=sys.stderr)
            for line in out.read_text().splitlines():
                row = json.loads(line)
                if f"{Path(row['file']).name}:{row['line']}" in args.skip:
                    continue
                if Path(row["file"]).name in ("X87Single.h", "FloatToInt.h"):
                    continue  # the marks' own implementation
                if row["t"] == "skip":
                    notes[(row["file"], row["line"], row["why"])] = row
                else:
                    edits[(row["file"], row["t"], row["begin"], row.get("end"))] = row
    by_file = collections.defaultdict(list)
    for row in edits.values():
        by_file[row["file"]].append(row)
    for path, rows in sorted(by_file.items()):
        kinds = collections.Counter(r["t"] for r in rows)
        print(f"{Path(path).name:28} {kinds['op']:4} operations {kinds['sqrt']:3} sqrt {kinds['fild']:4} integer loads")
        if not args.dry_run:
            Path(path).write_bytes(apply(Path(path).read_bytes(), rows))
    for (path, line, why), row in sorted(notes.items()):
        print(f"review {Path(path).name}:{line}: {why}" + (f" ({row['macro']})" if row["macro"] else ""))


if __name__ == "__main__":
    main()
