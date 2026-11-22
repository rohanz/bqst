#!/usr/bin/env python3
"""Compare the real BQST processor before and after the double-precision Grit change,
ordinary and Lab builds, on both macOS slices.

"pre" is the last float-precision Grit source (commit REV); "current" is the worktree.
Legacy and Hybrid Grit are expected to change; everything else must stay byte-identical:
  - every Captured Grit matrix case, and the Lab missing-revision migration case
    (which restores Captured), must match pre byte-for-byte;
  - every Legacy/Hybrid case must render finite audio (the harness fails otherwise).
It also prints the before/after low-frequency level table and the Grit CPU cost.

Usage: uv run python tests/grit_equivalence/run.py [arm64|x86_64 ...] [--levels] [--cpu]
Work files go to $GRIT_EQUIVALENCE_DIR (default: ignored build/grit-equivalence).
Remove <dir>/snapshots to refresh the retained sources.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('GRIT_EQUIVALENCE_DIR', ROOT / 'build/grit-equivalence')).resolve()
SNAP = WORK / 'snapshots'
REV = 'ee0b31e'  # last float-precision Grit source

def run(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, check=True, **kwargs)

def changed_by_precision(name):
    """Cases whose audio the precision change is allowed to alter."""
    if name.startswith('migration_'):
        return None  # depends on the build variant; see below
    return '_r1_' not in name

args = sys.argv[1:]
want_levels = '--levels' in args
want_cpu = '--cpu' in args
archs = [a for a in args if not a.startswith('--')] or ['arm64', 'x86_64']

# Recreate source snapshots only when requested/no snapshot; both architectures use the
# same manifest. Never restore old source into the worktree's tracked paths.
if not (SNAP / 'manifest.json').exists():
    for version in ('pre', 'current'):
        dest = SNAP / version
        dest.mkdir(parents=True, exist_ok=True)
        run(['rsync', '-a', '--exclude=build*', '--exclude=dist', '--exclude=.git',
             '--exclude=.room', '--exclude=external', str(ROOT) + '/src/', str(dest) + '/src/'])
    paths = run(['git', 'ls-tree', '-r', '--name-only', REV, 'src'], capture_output=True, text=True).stdout.splitlines()
    shutil.rmtree(SNAP / 'pre/src')
    for path in paths:
        target = SNAP / 'pre' / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(run(['git', 'show', f'{REV}:{path}'], capture_output=True).stdout)
    manifest = {'pre_commit': run(['git', 'rev-parse', REV], capture_output=True, text=True).stdout.strip()}
    for version in ('pre', 'current'):
        manifest[version] = {str(p.relative_to(SNAP/version)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted((SNAP/version/'src').rglob('*')) if p.is_file()}
    (SNAP/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')

manifest = json.loads((SNAP/'manifest.json').read_text())
current_hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted((ROOT/'src').rglob('*')) if p.is_file()}
if current_hashes != manifest['current']:
    raise SystemExit(f'Current src changed: remove {SNAP} and rerun')

juce = ROOT / 'external/JUCE'
if not (juce/'CMakeLists.txt').exists():
    raise SystemExit('external/JUCE is empty: fill the submodule first')

failed = False
for arch in archs:
    if arch not in ('arm64', 'x86_64'):
        raise SystemExit('architecture must be arm64 or x86_64')
    build = WORK / arch
    run(['cmake', '-S', str(ROOT/'tests/grit_equivalence'), '-B', str(build),
         '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_OSX_ARCHITECTURES={arch}',
         '-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13', f'-DJUCE_ROOT={juce}',
         f'-DSNAPSHOTS={SNAP}', f'-DHARNESS={ROOT}/tests/grit_equivalence.cpp'])
    run(['cmake', '--build', str(build), '-j', '3'])
    exe = lambda version, lab=0: ['/usr/bin/arch', f'-{arch}', str(build/f'{version}_lab{lab}')]
    results = {}
    for lab in (0, 1):
        for version in ('pre', 'current'):
            out = WORK/'renders'/arch/f'{version}_lab{lab}'
            if out.exists():
                shutil.rmtree(out)
            run(exe(version, lab) + [str(out)])
        old = WORK/'renders'/arch/f'pre_lab{lab}'
        new = WORK/'renders'/arch/f'current_lab{lab}'
        names = sorted(p.name for p in old.glob('*.f32'))
        assert len(names) == 194 and names == sorted(p.name for p in new.glob('*.f32'))
        different = {name for name in names if (old/name).read_bytes() != (new/name).read_bytes()}
        # Lab's migration restores Captured (unchanged); production's restores Legacy (changed).
        allowed = {n for n in names if changed_by_precision(n) or (n.startswith('migration_') and lab == 0)}
        unexpected = sorted(different - allowed)
        unchanged_grit = sorted(allowed - different)
        identical = sorted(set(names) - allowed)
        combined = hashlib.sha256()
        for name in identical:
            combined.update((new/name).read_bytes())
        results[f'lab{lab}'] = {'cases': len(names), 'must_match': len(identical),
                                'unexpected_differences': unexpected,
                                'grit_cases_changed': len(different & allowed),
                                'grit_cases_unchanged': unchanged_grit,
                                'sha256_unchanged_outputs': combined.hexdigest()}
        failed = failed or bool(unexpected) or bool(unchanged_grit)
    (WORK/f'result-{arch}.json').write_text(json.dumps(results, indent=2)+'\n')
    print(arch, json.dumps(results), flush=True)

    if want_levels:
        level = {}
        for version in ('pre', 'current'):
            text = run(exe(version) + ['--levels'], capture_output=True, text=True).stdout
            for line in text.splitlines():
                if line.startswith('LEVEL,'):
                    _, rev, drive, os_, hz, rms = line.split(',')
                    level[(version, int(rev), int(drive), int(os_), int(hz))] = float(rms)
        import math
        for rev, label in ((2, 'Hybrid (default)'), (0, 'Legacy')):
            print(f'\n{arch} {label}: level change after - before, dB')
            print('| Drive | OS | 20 Hz | 50 Hz | 100 Hz | 200 Hz |\n|---:|---:|---:|---:|---:|---:|')
            spread = 0.0
            for drive in (6, 12, 18):
                for os_ in range(4):
                    cells = []
                    for hz in (20, 50, 100, 200):
                        before = level[('pre', rev, drive, os_, hz)]
                        after = level[('current', rev, drive, os_, hz)]
                        cells.append(f'{20*math.log10(after/before):+.4f}')
                        spread = max(spread, abs(20*math.log10(after/level[('current', rev, drive, 0, hz)])))
                    print(f'| {drive} | {["Off","2x","4x","8x"][os_]} | ' + ' | '.join(cells) + ' |')
            print(f'{arch} {label}: worst after spread across oversampling {spread:.6f} dB')
            failed = failed or spread >= 0.05

    if want_cpu:
        cpu = {}
        for repeat in range(3):  # alternate the order to share drift between versions
            for version in (('pre', 'current') if repeat % 2 == 0 else ('current', 'pre')):
                text = run(exe(version) + ['--cpu'], capture_output=True, text=True).stdout
                for line in text.splitlines():
                    if line.startswith('CPU,'):
                        _, rev, os_, pct = line.split(',')
                        cpu.setdefault((version, int(rev), int(os_)), []).append(float(pct))
        print(f'\n{arch} processBlock CPU, % of one core at 48 kHz/256, Drive 12, median of runs')
        print('| Grit | OS | Before | After | Change |\n|---|---|---:|---:|---:|')
        for rev, label in ((2, 'Hybrid'), (0, 'Legacy')):
            for os_ in range(4):
                before = statistics.median(cpu[('pre', rev, os_)])
                after = statistics.median(cpu[('current', rev, os_)])
                print(f'| {label} | {["Off","2x","4x","8x"][os_]} | {before:.3f} | {after:.3f} | '
                      f'{100*(after/before-1):+.1f}% |')

if failed:
    raise SystemExit('grit equivalence: FAILED (see results above)')
