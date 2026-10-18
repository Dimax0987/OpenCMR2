#!/usr/bin/env python3
"""Run monkey_probe over many seeds and group what goes wrong.

Each seed plays the game with random input (tests/monkey_probe.cpp). A run
is ok when it reaches its frame count with no sanitizer report; otherwise it
is a crash (a signal), a sanitizer report (AddressSanitizer or UBSan), a
hang (no frame for five minutes), or a quit (the game left). Failures are grouped
by the game functions at the top of their stack, each with the seeds that
reproduce it:

    tools/monkey.py --probe build/linux-debug/tests/monkey_probe --data ~/cmr2game \\
        --seeds 1-64 --frames 60000 --output build/monkey

The sanitizer build (linux-debug, see docs/DEVELOPMENT.md) finds far more
than the release one. A seed runs the same again:

    XDG_DATA_HOME=$(mktemp -d) build/linux-debug/tests/monkey_probe ~/cmr2game SEED FRAMES
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

FRAME = re.compile(r'#\d+ 0x[0-9a-f]+ in (\S+?)(?:\(.*?\))? (\S+?):(\d+)')
RUNTIME_ERROR = re.compile(r'(\S+?):(\d+):\d+: runtime error: (.*)')
SUMMARY = re.compile(r'monkey: (done|quit) after (\d+) frames, (\d+) ticks, (\d+) driving, (\d+) races; '
                     r'modes ([\d ]*); game modes ([\d ]*); menus ([-\d ]*)')


def seeds(text):
    out = []
    for part in text.split(','):
        lo, _, hi = part.partition('-')
        out.extend(range(int(lo), int(hi or lo) + 1))
    return out


def game_frames(lines, start):
    """Function and source line of the game's frames in the stack at start."""
    frames = []
    for line in lines[start:start + 60]:
        m = FRAME.search(line)
        if m:
            path = m.group(2)
            if '/game/' in path or '/src/' in path:
                frames.append(f'{m.group(1)} {Path(path).name}:{m.group(3)}')
        elif frames and not line.strip():
            break
    return frames


def classify(log, code):
    lines = log.splitlines()
    for i, line in enumerate(lines):
        if 'ERROR: AddressSanitizer' in line:
            kind = re.search(r'AddressSanitizer: ([\w-]+)', line)
            frames = game_frames(lines, i)
            return 'sanitizer', f'asan {kind.group(1) if kind else "?"}: ' + ' < '.join(frames[:3])
        m = RUNTIME_ERROR.search(line)
        if m:
            return 'sanitizer', f'ubsan {Path(m.group(1)).name}:{m.group(2)}: {m.group(3)[:80]}'
    if code == 124 or 'monkey: hang' in log:
        frame = re.search(r'monkey: hang at frame (\d+)', log)
        return 'hang', f'hang (last mode/menu before frame {frame.group(1) if frame else "?"})'
    if 'monkey: quit' in log:
        return 'quit', 'the game quit'
    if code != 0 or 'monkey: done' not in log:
        tail = ' | '.join(l for l in lines[-3:] if l.strip())[:160]
        return 'crash', f'exit {code}: {tail}'
    return 'ok', ''


def run(probe, data, output, frames, timeout, env_extra, seed):
    run_dir = output / f'seed-{seed}'
    (run_dir / 'user').mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('OPENCMR2_PROBE_')}
    env.update(OPENCMR2_HEADLESS='1', SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               XDG_DATA_HOME=str(run_dir / 'user'),
               ASAN_OPTIONS='detect_leaks=0:halt_on_error=0', UBSAN_OPTIONS='print_stacktrace=1')
    env.update(env_extra)
    log_path = run_dir / 'run.log'
    with log_path.open('w') as log:
        try:
            code = subprocess.run([str(probe), str(data), str(seed), str(frames)], env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = 124
    text = log_path.read_text(encoding='latin-1')
    kind, signature = classify(text, code)
    summary = SUMMARY.search(text)
    return {'seed': seed, 'kind': kind, 'signature': signature, 'log': str(log_path),
            'races': int(summary.group(5)) if summary else 0,
            'driving': int(summary.group(4)) if summary else 0,
            'game_modes': summary.group(7).split() if summary else [],
            'menus': summary.group(8).split() if summary else []}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seeds', default='1-16', help='e.g. 1-64 or 3,8,20-30')
    parser.add_argument('--frames', type=int, default=60000)
    parser.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument('--timeout', type=int, default=3600, help='seconds per seed')
    args = parser.parse_args()

    # The sanitizer build links the shared AddressSanitizer runtime.
    env_extra = {}
    try:
        lib = subprocess.run(['clang', '-print-file-name=libclang_rt.asan.so'], capture_output=True,
                             text=True).stdout.strip()
        if lib and Path(lib).exists():
            env_extra['LD_LIBRARY_PATH'] = str(Path(lib).parent)
    except OSError:
        pass

    args.output.mkdir(parents=True, exist_ok=True)
    probe, data = args.probe.resolve(), args.data.expanduser().resolve()
    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda s: run(probe, data, args.output.resolve(), args.frames, args.timeout,
                                                  env_extra, s), seeds(args.seeds)))

    groups = {}
    for r in results:
        if r['kind'] != 'ok':
            groups.setdefault((r['kind'], r['signature']), []).append(r)
    menus = set().union(*(r['menus'] for r in results))
    modes = set().union(*(r['game_modes'] for r in results))
    print(f"{len(results)} seeds, {args.frames} frames each: "
          f"{sum(r['kind'] == 'ok' for r in results)} ok, {len(results) - sum(r['kind'] == 'ok' for r in results)} not")
    print(f"coverage: {sum(r['races'] for r in results)} races, {sum(r['driving'] for r in results)} driving steps, "
          f"game modes {' '.join(sorted(modes, key=int))}, {len(menus)} menus")
    for (kind, signature), runs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print(f"\n{kind}: {signature}\n  seeds {' '.join(str(r['seed']) for r in runs)}\n  log {runs[0]['log']}")
    (args.output / 'report.json').write_text(json.dumps(results, indent=1) + '\n')
    return 1 if groups else 0


if __name__ == '__main__':
    sys.exit(main())
