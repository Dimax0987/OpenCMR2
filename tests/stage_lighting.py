#!/usr/bin/env python3
"""Stage lighting records vs the original in-place relocation.

Scene_LoadLighting turns the 32-bit offsets of the lighting file into
pointers; OpenCMR2 copies the zones, items and mesh headers into native
records instead. stage_probe (OPENCMR2_PROBE_LIGHTING) prints pointer-free
digests of the result after the stage loads: zones, items, item owners as
(sector, position in its object list), zone mesh data (without the diffuse
colours, which the scene lights rewrite for the sectors in view) and the
sectors' shadow meshes. The expected values were produced by the original
algorithm (the 32-bit port before the native copies) and must hold on every
target.

    tests/stage_lighting.py --probe build/linux/tests/stage_probe --data GAME
"""
import argparse
import concurrent.futures
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# (rally cursor, stage cursor): zone count and the five digests.
EXPECTED = {
    (0, 0): "210 a3388df08a525b2b 48c17d951e368984 7b6fd26cda701288 b637b824908c9d27 611e6807c41a0eb0",
    (0, 1): "174 574c213c19b8685a 6235d27833e8034f 8b2b8d3c0d55df46 78aa08174180e34b b64f5c00449bf1d6",
    (1, 0): "194 f0ae41f52861f3eb ca86274ce1c4c1e9 4962459863d633fe 1d35759cfbd1483a e526f3e706f7f6f9",
    (2, 0): "150 4805247a7a683e7e fcaa051a100e7296 2ae24814d74c0197 c677cfca329b8a07 a78e58bede1b9b98",
    (3, 0): "157 fcd8c3f3a566061c d1585a38d6b295a7 5c77c0f9205294b8 d8c04c7b808207b1 3f6caeb4dac4b138",
}


def run(probe, data, rally, stage):
    with tempfile.TemporaryDirectory(prefix="opencmr2-lighting-") as user:
        env = dict(os.environ, XDG_DATA_HOME=user, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   OPENCMR2_HEADLESS="1", OPENCMR2_PROBE_LIGHTING="1",
                   OPENCMR2_PROBE_CURSORS=f"36:{rally},37:{stage}")
        result = subprocess.run([probe, data, str(Path(user) / "trace.jsonl"), "60", "60"], env=env,
                                capture_output=True, text=True, timeout=900)
    for line in result.stderr.splitlines():
        if line.startswith("probe: lighting "):
            return line[len("probe: lighting "):].replace(" zones", "")
    return "no lighting line (exit %d)" % result.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--probe", required=True)
    ap.add_argument("--data", required=True)
    args = ap.parse_args()
    with concurrent.futures.ThreadPoolExecutor(len(EXPECTED)) as pool:
        got = dict(zip(EXPECTED, pool.map(lambda k: run(args.probe, args.data, *k), EXPECTED)))
    bad = [k for k in EXPECTED if got[k] != EXPECTED[k]]
    for k in bad:
        print(f"rally {k[0]} stage {k[1]}: expected {EXPECTED[k]}\n{' ' * 20}got {got[k]}")
    if bad:
        return 1
    print(f"stage lighting matches the original relocation on {len(EXPECTED)} stages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
