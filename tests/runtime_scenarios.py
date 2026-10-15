#!/usr/bin/env python3
"""Exercise the real host, arcade ghost, replay and enlarged preliminary map paths."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

SCENARIOS = {
    'saved-game-create': (500, {'OPENCMR2_PROBE_SAVE_SESSION': '1',
                                'OPENCMR2_PROBE_EXPECT_MODE': '2'}),
    'saved-game-load': (500, {'OPENCMR2_PROBE_CURSORS': '88:0,218:1',
                              'OPENCMR2_PROBE_EXPECT_MODE': '2'}),
    'preliminary-map': (500, {'OPENCMR2_PROBE_EXPAND_MAP': '1'}),
    'arcade-time-trial': (500, {'OPENCMR2_PROBE_CURSORS': '0:2,88:2,29:0',
                              'OPENCMR2_PROBE_EXPECT_CARS': '2',
                              'OPENCMR2_PROBE_EXPECT_MODE': '7'}),
    'italy-arcade-championship': (2500, {'OPENCMR2_PROBE_CURSORS': '0:2,88:0',
                                      'OPENCMR2_PROBE_EXPECT_CARS': '6',
                                      'OPENCMR2_PROBE_EXPECT_MODE': '5',
                                      'OPENCMR2_PROBE_HEADLIGHT_CHEAT': '1'}),
    'multiplayer-host': (500, {'OPENCMR2_PROBE_CURSORS': '0:3,33:2,60:0,53:1,61:6,19:2,62:3',
                             'OPENCMR2_PROBE_EXPECT_MODE': '8',
                             'OPENCMR2_PROBE_SESSION_NAME': 'TEST'}),
    'finland-championship-results': (1500, {'OPENCMR2_PROBE_CURSORS': '88:0',
                                           'OPENCMR2_PROBE_EXPECT_MODE': '0',
                                           'OPENCMR2_PROBE_FINISH_AT_TICK': '500'}),
    'finland-results': (1500, {'OPENCMR2_PROBE_CURSORS': '88:1',
                              'OPENCMR2_PROBE_EXPECT_MODE': '1',
                              'OPENCMR2_PROBE_FINISH_AT_TICK': '500'}),
    'recorded-replay': (1000, {'OPENCMR2_PROBE_REPLAY_AT_TICK': '500'}),
}
for mode, cursor, cars in [('single-race', 1, 6), ('time-trial', 2, 2)]:
    for track, country in enumerate(['italy', 'sweden', 'greece', 'australia',
                                      'finland', 'france', 'kenya', 'united-kingdom']):
        SCENARIOS[f'arcade-{mode}-{country}'] = (500, {
            'OPENCMR2_PROBE_CURSORS': f'0:2,88:{cursor},29:1,37:{track}',
            'OPENCMR2_PROBE_EXPECT_CARS': str(cars),
            'OPENCMR2_PROBE_EXPECT_MODE': '6' if cursor == 1 else '7',
            'OPENCMR2_PROBE_EXPECT_ARCADE_TRACK': str(track),
            'OPENCMR2_PROBE_UNLOCK_TRACKS': '1',
        })
# The two-car super special stages, where the start table index is negative.
for country, name in [(1, 'greece'), (3, 'sweden'), (5, 'kenya'), (7, 'united-kingdom')]:
    SCENARIOS[f'super-special-{name}'] = (500, {
        'OPENCMR2_PROBE_CURSORS': f'88:2,36:{country},37:10',
        'OPENCMR2_PROBE_EXPECT_CARS': '2',
        'OPENCMR2_PROBE_EXPECT_COUNTRY': str(country),
        'OPENCMR2_PROBE_EXPECT_STAGE': '10',
        'OPENCMR2_PROBE_UNLOCK_TRACKS': '1',
    })
REPLAY = re.compile(r'probe: replay tick=(\d+) playing=(-?\d+) lane=(-?\d+) frame=(-?\d+) length=(-?\d+)')


def verify_replay(log):
    samples = [tuple(map(int, match.groups())) for match in REPLAY.finditer(log)]
    playback = [s for s in samples if s[0] >= 500 and s[1]]
    finished = [s for s in samples if s[0] > 500 and not s[1]]
    # frame counts compressed control packets, not 25 Hz simulation steps.
    cursors = [(s[2], s[3]) for s in playback]
    if len(set(cursors)) < 3 or cursors != sorted(cursors):
        raise ValueError(f'replay did not advance continuously: {playback}')
    if not finished or finished[0][2] < 1 or finished[0][0] <= playback[-1][0]:
        raise ValueError(f'replay did not finish normally: {samples}')
    return {'distinct_packet_cursors': len(set(cursors)),
            'last_playing_tick': playback[-1][0], 'first_finished_tick': finished[0][0]}


def verify(probe, data, output):
    report = {}
    for name, (count, settings) in SCENARIOS.items():
        run = output / name
        run.mkdir(parents=True)
        env = {k: v for k, v in os.environ.items() if not k.startswith('OPENCMR2_PROBE_')}
        env.update(OPENCMR2_HEADLESS='1', SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', XDG_DATA_HOME=str(run / 'user'))
        if name == 'saved-game-load':
            env['XDG_DATA_HOME'] = str(output / 'saved-game-create' / 'user')
        env.update(settings)
        log_path = run / 'run.log'
        with log_path.open('w') as log:
            subprocess.run([str(probe), str(data), str(run / 'trace.jsonl'), '30', str(count)],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        log = log_path.read_text()
        if not re.search(rf'probe: {count} ticks, [1-9][0-9]* driving ticks, .*; complete', log):
            raise ValueError(f'{name}: no completed race: {log_path}')
        records = [json.loads(line) for line in (run / 'trace.jsonl').read_text().splitlines()]
        if len(records) != count or records[-1]['tick'] != count - 1:
            raise ValueError(f'{name}: incomplete trace')
        report[name] = {'ticks': count, 'driving_ticks': records[-1]['driving_tick']}
        if name == 'saved-game-create' and 'probe: saved garage session' not in log:
            raise ValueError('real garage session was not saved')
        if name == 'saved-game-load' and not all(
                f'probe: menu 218 ({count} entries)' in log for count in (2, 1)):
            raise ValueError('saved session was not selected through the Load menu')
        if name == 'preliminary-map' and 'probe: preliminary map fully expanded' not in log:
            raise ValueError('preliminary map never fully expanded')
        if name in ('finland-results', 'finland-championship-results'):
            match = re.search(r'probe: Finland stage 2 driving ticks=(\d+)', log)
            if not match or int(match[1]) < 100:
                raise ValueError('Finland results did not continue to stage 2')
            report[name]['next_stage_driving_ticks'] = int(match[1])
        if name == 'recorded-replay':
            report[name]['replay'] = verify_replay(log)
        print(f'{name}: PASS ({count} ticks)', flush=True)
    (output / 'acceptance.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
        verify(args.probe.resolve(), args.data.resolve(), args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='opencmr2-runtime-') as directory:
            verify(args.probe.resolve(), args.data.resolve(), Path(directory))


if __name__ == '__main__':
    main()
