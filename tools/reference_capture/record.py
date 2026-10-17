#!/usr/bin/env python3
"""Run one prepared Windows fixture in a disposable Wine prefix."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('prepared', type=Path)
    p.add_argument('side', choices=['original', 'decomp'])
    p.add_argument('--wineprefix', type=Path, required=True)
    p.add_argument('--timeout', type=int, default=180)
    p.add_argument('--fps', type=int, default=60)
    p.add_argument('--ticks', type=int, default=500)
    p.add_argument('--cursors', help='menu-id:cursor,..., as in the portable stage probe')
    p.add_argument('--expect-country', type=int, default=0)
    p.add_argument('--expect-stage', type=int, default=0)
    p.add_argument('--expect-cars', type=int, default=1)
    p.add_argument('--unlock-tracks', action='store_true', help='unlock every stage, such as the super specials')
    p.add_argument('--race-clock', type=int, default=100000,
                   help='virtual clock at the race start (ms); it seeds the start side of the super specials')
    p.add_argument('--finish-at', type=int, help='the player finishes after this many ticks')
    p.add_argument('--results-presses', type=int, help='after the finish, press Down this many times in the results menu')
    p.add_argument('--stage-records', action='store_true', help='split times in every stage record')
    p.add_argument('--x87-double', action='store_true',
                   help="keep Wine's x87 control word (0x027f) instead of Direct3D 7's single precision (0x007f)")
    args = p.parse_args()
    if not 10 <= args.fps <= 1000:
        raise ValueError('FPS must be in 10..1000')
    if not 1 <= args.ticks <= 100000 or not 1 <= args.expect_cars <= 8:
        raise ValueError('invalid tick target or expected car count')
    if args.cursors and not re.fullmatch(r'\d+:\d+(?:,\d+:\d+)*', args.cursors):
        raise ValueError('invalid menu cursors')
    output, prefix = args.prepared.resolve(), args.wineprefix.resolve()
    if not (prefix.is_relative_to(Path('/tmp')) or prefix.is_relative_to(output)):
        raise ValueError('use a disposable Wine prefix under /tmp or the prepared output')
    if not (prefix/'system.reg').exists():
        raise ValueError('initialize this disposable win32 prefix with wineboot first')
    run = output/args.side
    manifest = json.loads((output/'manifest.json').read_text())
    expected = manifest['runs'][args.side]
    for name, checksum in {'CMR2.exe': expected['executable_sha256'], 'capture.map': expected['address_map_sha256'],
                           'SPCMR2.dll': manifest['observer_sha256'], **expected.get('runtime_sha256', {})}.items():
        if sha(run/name) != checksum:
            raise ValueError(f'prepared fixture changed: {name}')
    if args.side == 'original' and sha(run/'SPCMR2_sp.dll') != manifest['silentpatch_sha256']:
        raise ValueError('SilentPatch DLL changed')
    if (run/'state.bin').exists() or (run/'capture.log').exists():
        raise ValueError('capture already exists; prepare a new output for a repeat')
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all',
               WINEDLLOVERRIDES='ddraw,d3dimm=b;msvcrt,msvcrt40,msadp32.acm=n,b')
    env.pop('WAYLAND_DISPLAY', None)
    env.pop('CMR_CAPTURE_DECOMP', None)
    env.pop('CMR_CAPTURE_TICKS', None)
    env.pop('CMR_CAPTURE_FPS', None)
    env.pop('CMR_CAPTURE_CURSORS', None)
    env.pop('CMR_CAPTURE_UNLOCK_TRACKS', None)
    for name in ('CMR_CAPTURE_FINISH_AT', 'CMR_CAPTURE_RESULTS_PRESSES', 'CMR_CAPTURE_STAGE_RECORDS'):
        env.pop(name, None)
    if args.finish_at:
        env['CMR_CAPTURE_FINISH_AT'] = str(args.finish_at)
    if args.results_presses:
        env['CMR_CAPTURE_RESULTS_PRESSES'] = str(args.results_presses)
    if args.stage_records:
        env['CMR_CAPTURE_STAGE_RECORDS'] = '1'
    env.pop('CMR_CAPTURE_RACE_CLOCK', None)
    env.pop('CMR_CAPTURE_X87_DOUBLE', None)
    env['CMR_CAPTURE_RACE_CLOCK'] = str(args.race_clock)
    if args.x87_double:
        env['CMR_CAPTURE_X87_DOUBLE'] = '1'
    env['CMR_CAPTURE_FPS'] = str(args.fps)
    env['CMR_CAPTURE_TICKS'] = str(args.ticks)
    if args.cursors:
        env['CMR_CAPTURE_CURSORS'] = args.cursors
    if args.unlock_tracks:
        env['CMR_CAPTURE_UNLOCK_TRACKS'] = '1'
    def registry(key, name, value):
        subprocess.run(['wine', 'reg', 'add', key, '/v', name, '/d', value, '/f'], env=env, check=True, stdout=subprocess.DEVNULL)
    registry(r'HKCU\Software\Wine\AppDefaults\CMR2.exe', 'Version', 'win2k')
    if args.side == 'decomp':
        # Standalone decompilation relocates code; applying the original-address
        # SilentPatch would corrupt it. This side is an additional control.
        print('Standalone decompilation control: no SilentPatch; original side is the patched reference.', flush=True)
        env['CMR_CAPTURE_DECOMP'] = '1'
        path = 'Z:' + str(run).replace('/', '\\')
        if len(path) >= 100:
            raise ValueError('legacy registry path exceeds its 100-byte buffer; choose a shorter output path')
        for key, value in {'Sku_Type': 'EUROPE', 'Language': 'English', 'Install_Version': 'Full',
                           'Game_HDPath': path, 'Game_CDPath': path}.items():
            registry(r'HKLM\Software\Codemasters\Colin McRae Rally 2', key, value)
    (run/'run-parameters.json').write_text(json.dumps({
        'fps': args.fps, 'ticks': args.ticks, 'cursors': args.cursors,
        'expected_country': args.expect_country, 'expected_stage': args.expect_stage,
        'expected_cars': args.expect_cars, 'unlock_tracks': args.unlock_tracks,
        'race_clock': args.race_clock, 'x87_control_word': '027f' if args.x87_double else '007f',
    }, indent=2) + '\n')
    with (run/'wine.log').open('x') as log:
        result = subprocess.run(['timeout', str(args.timeout)+'s', 'wine', 'CMR2.exe'], cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT)
    capture_log = (run/'capture.log').read_text()
    completed = re.search(rf'COMPLETE {args.ticks} ticks \((\d+) driving\)', capture_log)
    if args.results_presses:
        completed = re.search(r'results menu \d+ presses', capture_log) and completed
    scenario = f'scenario country={args.expect_country} stage={args.expect_stage} count={args.expect_cars}'
    if result.returncode or not completed or not 0 < int(completed.group(1)) <= args.ticks or scenario not in capture_log:
        raise ValueError(f'incomplete recording (exit {result.returncode}); inspect {run / "capture.log"} and wine.log')
    print(f'Complete recording: {run / "state.bin"} SHA256 {sha(run/"state.bin")}')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f'record failed: {error}', file=sys.stderr)
        sys.exit(2)
