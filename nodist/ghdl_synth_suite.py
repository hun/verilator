#!/usr/bin/env python3
# DESCRIPTION: Verilator: run GHDL testsuite VHDL designs through the Verilator frontend
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026 Hannes Klas
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

"""Run GHDL testsuite design files through Verilator's VHDL frontend.

The GHDL testsuite (https://github.com/ghdl/ghdl/tree/master/testsuite) is a large
corpus of VHDL design units.  This runner does not simulate them; it checks that the
Verilator VHDL frontend can analyze and lower each design unit with ``--lint-only``,
and categorizes the result:

  PASS          Verilator accepted the design
  UNSUPPORTED   the frontend reported "is not supported yet"
  GHDL_ERROR    GHDL rejected the sources (including expected-failure tests)
  NO_TOP        no entity matching the requested top was found
  ERROR         any other Verilator failure

Use ``--include-testbenches`` to also lint the ``tb_*.vhdl`` testbenches.  Those use
simulation constructs (wait/assert/report) that the synthesizable subset does not
model, so most will report UNSUPPORTED.

Examples:
  nodist/ghdl_synth_suite.py --testsuite ../ghdl/testsuite --only 'cnt*'
  nodist/ghdl_synth_suite.py --suite synth --jobs 16 --limit 50
  nodist/ghdl_synth_suite.py --include-testbenches --only 'case01'
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ENTITY_RE = re.compile(r'^\s*entity\s+([A-Za-z][A-Za-z0-9_]*)\s+is\b',
                       re.IGNORECASE | re.MULTILINE)
COMMENT_RE = re.compile(r'--[^\n]*')


def repo_root():
    return Path(__file__).resolve().parent.parent


def entity_names(path):
    try:
        text = path.read_text(errors='replace')
    except OSError:
        return []
    # GHDL lowercases identifiers in its XML, and so does the emitted module name
    return [name.lower() for name in ENTITY_RE.findall(COMMENT_RE.sub('', text))]


def classify(returncode, output):
    if returncode == 0:
        return 'PASS', ''
    if 'is not supported yet' in output or 'UNSUPPORTED' in output:
        detail = next((line.strip() for line in output.splitlines()
                       if 'is not supported yet' in line or 'UNSUPPORTED' in line), '')
        return 'UNSUPPORTED', detail
    if 'GHDL failed to analyze' in output:
        detail = next((line.strip() for line in output.splitlines()
                       if line.startswith('%Error:') and 'GHDL failed' in line), '')
        return 'GHDL_ERROR', detail
    if ('was not found in design' in output or 'No top module' in output
            or "Can't find module" in output):
        return 'NO_TOP', ''
    detail = next((line.strip() for line in output.splitlines()
                   if line.startswith('%Error')), '')
    return 'ERROR', detail


def run_one(job):
    (suite, testdir, top, files, args) = job
    workdir = Path(args.outdir) / suite / f'{testdir.name}__{top}'
    workdir.mkdir(parents=True, exist_ok=True)
    cmd = [
        args.verilator,
        '--language',
        'vhdl',
        '--lint-only',
        '-Wno-fatal',
        '--top-module',
        top,
        '-Mdir',
        str(workdir),
    ] + [f.name for f in files]
    env = os.environ.copy()
    if args.ghdl:
        env['VERILATOR_GHDL'] = args.ghdl
    started = time.monotonic()
    try:
        result = subprocess.run(cmd,
                                cwd=testdir,
                                env=env,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                timeout=args.timeout,
                                check=False)
        output = result.stdout.decode(errors='replace')
        status, detail = classify(result.returncode, output)
        returncode = result.returncode
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or b'').decode(errors='replace')
        status, detail = 'ERROR', f'timeout after {args.timeout}s'
        returncode = -1
    elapsed = time.monotonic() - started
    (workdir / 'verilator.log').write_text(output)
    return {
        'suite': suite,
        'test': testdir.name,
        'top': top,
        'status': status,
        'detail': detail,
        'returncode': returncode,
        'seconds': elapsed,
        'log': str(workdir / 'verilator.log'),
    }


def main():
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--testsuite',
                        default=os.environ.get('GHDL_TESTSUITE',
                                               str(root.parent / 'ghdl' / 'testsuite')),
                        help='GHDL testsuite directory (default: $GHDL_TESTSUITE or '
                        '../ghdl/testsuite)')
    parser.add_argument('--suite', default='synth', help='testsuite subdirectory (default: synth)')
    parser.add_argument('--verilator',
                        default=str(root / 'bin' / 'verilator'),
                        help='Verilator executable (default: bin/verilator)')
    parser.add_argument('--ghdl', default=None, help='GHDL executable (sets VERILATOR_GHDL)')
    parser.add_argument('--outdir',
                        default=str(root / 'obj_dir' / 'ghdl_suite'),
                        help='directory for logs (default: obj_dir/ghdl_suite)')
    parser.add_argument('--only', action='append', default=[],
                        help='only run test directories matching this glob (repeatable, '
                        'comma separated)')
    parser.add_argument('--start-at', default=None, help='skip tests sorting before this name')
    parser.add_argument('--limit', type=int, default=0, help='maximum number of test directories')
    parser.add_argument('--jobs', type=int, default=min(32, os.cpu_count() or 1),
                        help='parallel jobs (default: min(32, CPUs))')
    parser.add_argument('--timeout', type=int, default=120, help='per-run timeout in seconds')
    parser.add_argument('--include-testbenches', action='store_true',
                        help='also lint tb_*.vhdl testbenches')
    parser.add_argument('--fail-on', choices=['none', 'error', 'any'], default='none',
                        help='exit non-zero on ERROR (error) or any non-PASS (any)')
    parser.add_argument('--list', action='store_true', help='list test directories and tops')
    args = parser.parse_args()

    suite_dir = Path(args.testsuite) / args.suite
    if not suite_dir.is_dir():
        parser.error(f'testsuite directory not found: {suite_dir}')
    if not Path(args.verilator).is_file():
        parser.error(f'Verilator executable not found: {args.verilator}')

    patterns = []
    for item in args.only:
        patterns.extend(p for p in item.split(',') if p)

    testdirs = sorted(p for p in suite_dir.iterdir() if p.is_dir())
    if args.start_at:
        testdirs = [p for p in testdirs if p.name >= args.start_at]
    if patterns:
        import fnmatch
        testdirs = [p for p in testdirs
                    if any(fnmatch.fnmatch(p.name, pat) for pat in patterns)]
    if args.limit:
        testdirs = testdirs[:args.limit]

    jobs = []
    skipped = []
    for testdir in testdirs:
        design = sorted(f for f in testdir.glob('*.vhdl')
                        if not f.name.startswith(('tb_', 'syn_')))
        tb_files = sorted(testdir.glob('tb_*.vhdl')) if args.include_testbenches else []
        entity_files = {}
        for f in design + tb_files:
            for name in entity_names(f):
                entity_files.setdefault(name, []).append(f)
        tops = sorted(entity_files)
        if not tops:
            skipped.append((testdir.name, 'no entity found'))
            continue
        for top in tops:
            # GHDL's testsuite analyzes each top's own file, not every file in the
            # directory; passing all of them would merge alternative implementations
            candidates = entity_files[top]
            chosen = sorted(candidates,
                            key=lambda f: (0 if f.stem.lower() == top else 1, f.name))[0]
            if chosen.name.startswith('tb_'):
                # A testbench top also needs the design entity it exercises
                design_name = chosen.stem[3:].lower()
                designs = [f for f in design if f.stem.lower() == design_name]
                files = designs[:1] + [chosen]
            else:
                files = [chosen]
            jobs.append((args.suite, testdir, top, files, args))

    if args.list:
        for suite, testdir, top, _files, _args in jobs:
            print(f'{testdir.name}\t{top}')
        for name, why in skipped:
            print(f'{name}\tSKIP ({why})')
        return 0

    if not jobs:
        print('No matching tests', file=sys.stderr)
        return 1

    Path(args.outdir).mkdir(parents=True, exist_ok=True)
    print(f'Running {len(jobs)} cases from {suite_dir} with {args.jobs} jobs')

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, job) for job in jobs]
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            line = f"{result['status']:<12} {result['suite']}/{result['test']}/{result['top']}"
            if result['detail']:
                line += f"  {result['detail']}"
            print(line)

    results.sort(key=lambda r: (r['suite'], r['test'], r['top']))
    report = Path(args.outdir) / args.suite / 'report.tsv'
    report.parent.mkdir(parents=True, exist_ok=True)
    with report.open('w') as f:
        f.write('suite\ttest\ttop\tstatus\tseconds\tdetail\tlog\n')
        for r in results:
            detail = r['detail'].replace('\t', ' ')
            f.write(f"{r['suite']}\t{r['test']}\t{r['top']}\t{r['status']}\t"
                    f"{r['seconds']:.2f}\t{detail}\t{r['log']}\n")

    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
    print()
    print(f"Summary: {len(results)} cases, " +
          ', '.join(f'{k}={v}' for k, v in sorted(counts.items())))
    if skipped:
        print(f'Skipped {len(skipped)} test directories without entities')
    print(f'Report: {report}')

    if args.fail_on == 'any' and any(r['status'] != 'PASS' for r in results):
        return 1
    if args.fail_on == 'error' and any(r['status'] == 'ERROR' for r in results):
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
