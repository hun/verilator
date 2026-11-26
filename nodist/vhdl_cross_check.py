#!/usr/bin/env python3
# DESCRIPTION: Verilator: cross-check the VHDL frontend against GHDL and NVC on all three test suites
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of either the GNU Lesser General Public License Version 3
# or the Perl Artistic License Version 2.0.
# SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
"""Run the tests of NVC, GHDL and Verilator's own VHDL tests through all three tools.

GHDL and NVC are the reference: where they agree and Verilator does not, the case is a
divergence.  Each case goes through two stages.  The analysis stage analyzes the files and
elaborates the top with every tool.  The run stage builds and runs the cases all three tools
accepted, and compares the text of the reports and assertions, in order.

Buckets, worst first:

  CRASH            Verilator hit an internal error, a signal or ran out of memory
  ACCEPTS_ILLEGAL  both references reject, Verilator accepts
  REJECTS_LEGAL    both references accept, Verilator reports an error
  RUN_DIVERGE      all three accept, the report text or the failure status differs
  UNSUP            both references accept, Verilator says Unsupported (a gap, not a bug)
  REF_DISAGREE     GHDL and NVC differ, so Verilator is not judged
  TIMEOUT          a tool ran out of time
  MATCH            all three agree
  KNOWN            a divergence listed in the known-divergence file
  SKIP             the case needs something this script does not drive

Examples:
  nodist/vhdl_cross_check.py --nvc-dir ../nvc --only 'nvc/wait*'
  nodist/vhdl_cross_check.py --ghdl-dir ../ghdl --stage analysis --jobs 8
  nodist/vhdl_cross_check.py --out run.tsv --baseline last.tsv   # only what is new
"""

import argparse
import concurrent.futures
import fnmatch
import hashlib
import json
import os
import re
import resource
import shlex
import signal
import subprocess
import sys
import threading
from pathlib import Path
from typing import List, Optional

ROOT = Path(__file__).resolve().parent.parent
SEVERITY = ['CRASH', 'ACCEPTS_ILLEGAL', 'REJECTS_LEGAL', 'RUN_DIVERGE', 'UNSUP', 'REF_DISAGREE',
            'TIMEOUT', 'KNOWN', 'SKIP', 'MATCH']
MEM_LIMIT = 6 * 1024**3  # per Verilator process, so a runaway fails one case
OUTPUT_LIMIT = 64 * 1024**2  # per command; a simulation that prints without end counts as timed out
STD_GHDL = {'1993': '93c', '2000': '00', '2002': '02', '2008': '08', '2019': '19'}
ENTITY_RE = re.compile(r'^\s*entity\s+(\w+)\s+is\b', re.I | re.M)
GHDL_MSG_RE = re.compile(r':@\S+:\((?:report|assertion) (\w+)\): ?(.*)$')
NVC_MSG_RE = re.compile(r'^\*\* (Note|Warning|Error|Fatal|Failure): \S+: (.*)$')
OURS_ASSERT_TEXTS = {'assertion violation', 'assertion failed'}
OURS_FATAL_RE = re.compile(r'^\[\d+\] %(?:fatal|error): \S+: assertion failed in \S+: ?(.*)$')
# Diagnostics for constructs the frontend refuses without the word Unsupported
REFUSED_RE = re.compile(r"unknown identifier 'now'")
GHDL_UNPORTABLE = re.compile(r'^(--ieee|--std=(?!\d)|-fsynopsys|-frelaxed|--force-analysis|-P|--work)')

class Case:
    """One design: ordered analysis steps (lists of files), then an optional top to run."""

    def __init__(self, corpus, name, workdir_src):
        self.corpus = corpus
        self.name = name
        self.src = Path(workdir_src)
        self.std = '2008'
        self.steps: List[List[str]] = []  # each a list of file names relative to src
        self.top: Optional[str] = None
        self.generics: List[str] = []  # 'NAME=VALUE'
        self.stop_time: Optional[str] = None
        self.run = True  # False: analysis and elaboration only
        self.skip: Optional[str] = None
        self.relaxed = False

    @property
    def key(self):
        return self.corpus + '/' + self.name

    def files(self):
        return [f for step in self.steps for f in step]


# ---------------------------------------------------------------- corpora


def load_nvc(nvc_dir):
    regress = Path(nvc_dir) / 'test' / 'regress'
    cases = []
    for line in (regress / 'testlist.txt').read_text().splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[0].startswith('#'):
            continue
        name, flags = parts[0], parts[1].split(',')
        case = Case('nvc', name, regress)
        case.steps = [[name + '.vhd']]
        case.top = name
        case.std = '1993'
        for flag in flags:
            opt, _, val = flag.partition('=')
            if opt in ('2000', '2002', '2008', '2019'):
                case.std = opt
            elif opt == 'stop':
                case.stop_time = val
            elif opt.startswith('g') and val:
                case.generics.append(opt[1:] + '=' + val)
            elif opt in ('normal', 'gold', 'fail', 'slow', 'seed', 'dump-arrays', 'O0'):
                pass
            else:
                case.skip = 'nvc flag ' + opt
        if not (regress / (name + '.vhd')).is_file():
            case.skip = 'no source'
        cases.append(case)
    return cases


def load_ghdl(ghdl_dir):
    """Cases from gna test scripts that use only analyze, elab and simulate commands."""
    gna = Path(ghdl_dir) / 'testsuite' / 'gna'
    cases = []
    for script in sorted(gna.glob('*/testsuite.sh')):
        case = Case('ghdl', script.parent.name, script.parent)
        case.std = '1993'
        simple = True
        ran = False
        for raw in script.read_text(errors='replace').splitlines():
            text = raw.split('#', 1)[0].strip()
            if not text or text.startswith('. ') or text.split()[0] in ('echo', 'clean', 'exit'):
                continue
            try:
                words = shlex.split(text)
            except ValueError:
                simple = False
                break
            if words[0] == 'export':
                words = words[1:]
            cmd, args = words[0], words[1:]
            if cmd.startswith('GHDL_STD_FLAGS='):
                flags = shlex.split(cmd.split('=', 1)[1])
                if not apply_std_flags(case, flags):
                    simple = False
                    break
            elif cmd in ('analyze', 'analyze_failure'):
                flags = [a for a in args if a.startswith('-')]
                args = [a for a in args if not a.startswith('-')]
                if not apply_std_flags(case, flags) or not all(
                    (script.parent / a).is_file() for a in args):
                    simple = False
                    break
                case.steps.append(args)
            elif cmd in ('elab_simulate', 'elab_simulate_failure', 'elab', 'elab_failure'):
                tops = [a for a in args if not a.startswith('-')]
                for opt in (a for a in args if a.startswith('-')):
                    if opt.startswith('--stop-time='):
                        case.stop_time = opt.split('=', 1)[1]
                    elif opt.startswith('-g') and '=' in opt:
                        case.generics.append(opt[2:])
                    else:
                        simple = False
                if len(tops) != 1 or ran:
                    simple = False
                    break
                case.top = tops[0]
                case.run = cmd.startswith('elab_simulate')
                ran = True
            else:
                simple = False
                break
        if not simple or not case.steps:
            case.skip = 'script too complex'
        cases.append(case)
    return cases


def apply_std_flags(case, flags):
    """Take --std and -frelaxed from GHDL flags; False for any option NVC has no match for."""
    for flag in flags:
        std = re.fullmatch(r'--std=(\w+)', flag)
        if std:
            case.std = {'87': '1993', '93': '1993', '93c': '1993', '00': '2000', '02': '2002',
                        '08': '2008', '19': '2019'}.get(std.group(1), '1993')
        elif flag in ('-frelaxed', '-frelaxed-rules'):
            case.relaxed = True
        else:
            return False
    return True


def load_own():
    tests = ROOT / 'test_regress' / 't'
    cases = []
    for py in sorted(tests.glob('t_vhdl_*.py')):
        text = py.read_text()
        match = re.search(r"top_filename\s*=\s*'t/([^']+\.vhd[l]?)'", text)
        case = Case('own', py.stem, tests)
        if not match or not (tests / match.group(1)).is_file():
            case.skip = 'no single source'
            cases.append(case)
            continue
        case.steps = [[match.group(1)]]
        source = (tests / match.group(1)).read_text(errors='replace')
        case.std = '2019' if re.search(r"\b(view|converse)\b", source, re.I) else '2008'
        top = re.search(r"'--top-module',\s*'(\w+)'", text)
        case.top = top.group(1) if top else pick_top(tests / match.group(1))
        case.run = "'--binary'" in text and '.cpp' not in text
        case.generics = re.findall(r"'-G(\w+=[^']+)'", text)
        if not case.top:
            case.skip = 'no entity'
        cases.append(case)
    return cases


def pick_top(path):
    """The last entity that no other entity of the file instantiates."""
    text = re.sub(r'--[^\n]*', '', path.read_text(errors='replace'))
    names = ENTITY_RE.findall(text)
    free = [n for n in names if not re.search(r'\bentity\s+(?:work\.)?' + n + r'\b(?!\s+is)', text,
                                              re.I) and not re.search(r'\bcomponent\s+' + n + r'\b',
                                                                        text, re.I)]
    return (free or names or [None])[-1]


# ---------------------------------------------------------------- running tools


def run(cmd, cwd, timeout, limit_mem=False, env=None):
    """Run a command; returns (rc, output, timed_out).  rc is negative for a signal."""

    def limits():
        os.setsid()
        if limit_mem:
            resource.setrlimit(resource.RLIMIT_AS, (MEM_LIMIT, MEM_LIMIT))

    full_env = dict(os.environ, **(env or {}))
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            preexec_fn=limits, env=full_env)
    assert proc.stdout is not None
    output = bytearray()
    runaway = threading.Event()

    def kill():
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def pump():
        # Keep reading so the child never blocks, but keep only the start of the output
        for chunk in iter(lambda: proc.stdout.read(65536), b''):
            if runaway.is_set():
                continue
            output.extend(chunk[:OUTPUT_LIMIT - len(output)])
            if len(output) >= OUTPUT_LIMIT:
                runaway.set()
                kill()

    reader = threading.Thread(target=pump)
    reader.start()
    timed_out = False
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        kill()
        proc.wait()
    reader.join()
    proc.stdout.close()
    if timed_out or runaway.is_set():
        return -9, output.decode(errors='replace'), True
    return proc.returncode, output.decode(errors='replace'), False


class Tools:

    def __init__(self, args):
        self.args = args
        self.verilator = str(ROOT / 'bin' / 'verilator')
        self.env = {'VERILATOR_ROOT': str(ROOT)}
        self.versions = {}
        for tool in ('ghdl', 'nvc'):
            _, out, _ = run([tool, '--version'], None, 20)
            self.versions[tool] = out.splitlines()[0] if out else '?'
        stat = (ROOT / 'bin' / 'verilator_bin').stat()
        self.versions['ours'] = '%d-%d' % (stat.st_size, stat.st_mtime_ns)


def first_error(out):
    for line in out.splitlines():
        if re.search(r'error|fatal|%Error|Unsupported|Internal', line, re.I):
            return line.strip()[:200]
    return out.strip().splitlines()[0][:200] if out.strip() else ''


def ghdl_flags(case):
    return ['--std=' + STD_GHDL[case.std], '-fsynopsys'] + (['-frelaxed'] if case.relaxed else [])


def nvc_flags(case):
    return ['--std=' + case.std]


def nvc_analyze(case):
    return ['-a'] + (['--relaxed'] if case.relaxed else [])


def ref_analysis(tool, case, work, tools):
    """Verdict per analysis step and for the elaboration of the top, on one reference tool."""
    work.mkdir(parents=True, exist_ok=True)
    verdicts = []
    for step in case.steps:
        files = [str((case.src / f).resolve()) for f in step]
        if tool == 'ghdl':
            cmd = ['ghdl', '-a'] + ghdl_flags(case) + ['--workdir=' + str(work)] + files
        else:
            cmd = ['nvc'] + nvc_flags(case) + ['--work=' + str(work / 'work')
                                               ] + nvc_analyze(case) + files
        rc, out, tmo = run(cmd, work, tools.args.timeout)
        verdicts.append(['timeout' if tmo else 'ok' if rc == 0 else 'error', first_error(out)])
    if case.top:
        gen = ['-g' + g for g in case.generics]
        if tool == 'ghdl':
            cmd = ['ghdl', '-e'] + ghdl_flags(case) + ['--workdir=' + str(work)] + gen + [case.top]
        else:
            cmd = ['nvc'] + nvc_flags(case) + ['--work=' + str(work / 'work'), '-e'] + gen + [
                case.top]
        rc, out, tmo = run(cmd, work, tools.args.timeout)
        verdicts.append(['timeout' if tmo else 'ok' if rc == 0 else 'error', first_error(out)])
    return verdicts


def ours_status(rc, out, timed_out):
    if timed_out:
        return 'timeout'
    if 'may be caused by the earlier error' in out:  # follows a real diagnostic
        out = out.split('%Error: Internal Error', 1)[0]
    if re.search(r'Internal Error|Verilator aborted|Segmentation|terminate called|std::bad_alloc|'
                 r'failed to allocate|Cannot allocate', out) or rc < 0:
        return 'crash'
    first = first_error(out)
    if rc != 0 and ('Unsupported' in first or REFUSED_RE.search(first)):
        return 'unsup'
    return 'ok' if rc == 0 else 'error'


def ours_analysis(case, work, tools, ref_verdicts):
    """Lint the files of each step together with the earlier accepted ones."""
    work.mkdir(parents=True, exist_ok=True)
    verdicts = []
    accepted = []
    for idx, step in enumerate(case.steps):
        files = [str((case.src / f).resolve()) for f in accepted + step]
        cmd = [tools.verilator, '--language', 'vhdl', '--lint-only', '-Wno-fatal'] + files
        rc, out, tmo = run(cmd, work, tools.args.timeout, True, tools.env)
        # only an error in this step's files counts against it
        mine = [str((case.src / f).resolve()) for f in step]
        text = out if any(m in out for m in mine) or rc < 0 or tmo else ''
        status = ours_status(rc, text, tmo) if rc != 0 else 'ok'
        if rc != 0 and not text:
            status = 'ok'  # an error about an earlier file or a missing top
        verdicts.append([status, first_error(text)])
        if idx < len(ref_verdicts) and ref_verdicts[idx][0] == 'ok':
            accepted += step
    if case.top:
        files = [str((case.src / f).resolve()) for f in accepted]
        gen = ['-G' + g for g in case.generics]
        cmd = [tools.verilator, '--language', 'vhdl', '--lint-only', '-Wno-fatal', '--top-module',
               case.top] + gen + files
        rc, out, tmo = run(cmd, work, tools.args.timeout, True, tools.env)
        verdicts.append([ours_status(rc, out, tmo), first_error(out)])
    return verdicts


def parse_messages(tool, out):
    """The report and assertion messages in order, as [severity, text]."""
    msgs = []
    for line in out.splitlines():
        if tool == 'ghdl':
            match = GHDL_MSG_RE.search(line)
        elif tool == 'nvc':
            match = NVC_MSG_RE.match(line)
        else:
            stripped = line.strip()
            if not stripped or stripped.startswith('- ') or stripped.startswith('%Warning'):
                continue
            fatal = OURS_FATAL_RE.match(stripped)
            msgs.append(['failure' if fatal else '?', fatal.group(1) if fatal else stripped])
            continue
        if match and not re.fullmatch(r'(finish|stop) called', match.group(2).strip(), re.I):
            msgs.append([match.group(1).lower(), match.group(2).strip()])
    return msgs


def norm_text(text):
    text = re.sub(r'\s+', ' ', text.strip()).rstrip('.').lower()
    return '<assert>' if text in OURS_ASSERT_TEXTS else text


def run_ref(tool, case, work, tools):
    gen = ['-g' + g for g in case.generics]
    stop = ['--stop-time=' + case.stop_time.replace(' ', '')] if case.stop_time else []
    if tool == 'ghdl':
        cmd = ['ghdl', '--elab-run'] + ghdl_flags(case) + ['--workdir=' + str(work)
                                                            ] + gen + [case.top] + stop
    else:
        cmd = ['nvc'] + nvc_flags(case) + ['--work=' + str(work / 'work'), '-e'] + gen + [
            case.top, '-r'] + stop
    rc, out, tmo = run(cmd, work, tools.args.timeout)
    msgs = parse_messages(tool, out)
    failure = any(sev in ('failure', 'fatal') for sev, _ in msgs)
    return {'rc': rc, 'timeout': tmo, 'msgs': msgs, 'failure': failure}


def run_ours(case, work, tools):
    files = [str((case.src / f).resolve()) for f in case.files()]
    gen = ['-G' + g for g in case.generics]
    obj = work / 'obj'
    cmd = [tools.verilator, '--language', 'vhdl', '--binary', '--timing', '-Wno-fatal',
           '--top-module', case.top, '-Mdir', str(obj), '-j', '1', '-o', 'sim'] + gen + files
    rc, out, tmo = run(cmd, work, tools.args.build_timeout, True, tools.env)
    if rc != 0 or tmo:
        return {'build': ours_status(rc, out, tmo), 'detail': first_error(out)}
    rc, out, tmo = run([str(obj / 'sim')], work, tools.args.timeout, True)
    msgs = parse_messages('ours', out)
    return {'build': 'ok', 'rc': rc, 'timeout': tmo, 'msgs': msgs, 'failure': rc != 0}


# ---------------------------------------------------------------- classification


def classify_analysis(ref_g, ref_n, ours):
    """Worst bucket over the steps, with the first message behind it."""
    worst = ('MATCH', '')
    for idx, (vg, vn) in enumerate(zip(ref_g, ref_n)):
        vo = ours[idx] if idx < len(ours) else ['ok', '']
        if 'timeout' in (vg[0], vn[0], vo[0]):
            bucket, msg = 'TIMEOUT', 'step %d' % idx
        elif vg[0] != vn[0]:
            bucket, msg = 'REF_DISAGREE', 'step %d: ghdl %s / nvc %s' % (idx, vg[1] or 'ok',
                                                                         vn[1] or 'ok')
        elif vo[0] == 'crash':
            bucket, msg = 'CRASH', vo[1]
        elif vg[0] == 'ok' and vo[0] == 'unsup':
            bucket, msg = 'UNSUP', vo[1]
        elif vg[0] == 'ok' and vo[0] == 'error':
            bucket, msg = 'REJECTS_LEGAL', vo[1]
        elif vg[0] == 'error' and vo[0] == 'ok':
            bucket, msg = 'ACCEPTS_ILLEGAL', 'step %d: %s' % (idx, vg[1])
        else:
            continue
        if SEVERITY.index(bucket) < SEVERITY.index(worst[0]):
            worst = (bucket, msg)
    return worst


def compare_run(case, ref_g, ref_n, ours):
    if ours.get('timeout') and case.stop_time:  # Verilator has no run-time stop time
        return 'TIMEOUT', 'ours has no stop time %s' % case.stop_time
    if ours.get('timeout') and not (ref_g['timeout'] or ref_n['timeout']):
        return 'RUN_DIVERGE', 'ours did not finish, the references did'
    if ref_g['timeout'] or ref_n['timeout'] or ours.get('timeout'):
        return 'TIMEOUT', 'ghdl %s nvc %s ours %s' % (ref_g['timeout'], ref_n['timeout'],
                                                      ours.get('timeout'))
    texts = lambda res: [norm_text(t) for _, t in res['msgs']]
    if texts(ref_g) != texts(ref_n):
        return 'REF_DISAGREE', first_diff(texts(ref_g), texts(ref_n), 'ghdl', 'nvc')
    if ours['build'] == 'crash':
        return 'CRASH', ours['detail']
    if ours['build'] == 'unsup':
        return 'UNSUP', ours['detail']
    if ours['build'] != 'ok':
        return 'REJECTS_LEGAL', ours['detail']
    if ref_g['failure'] != ours['failure']:
        return 'RUN_DIVERGE', 'failure: refs %s ours %s' % (ref_g['failure'], ours['failure'])
    if texts(ref_g) != texts(ours):
        return 'RUN_DIVERGE', first_diff(texts(ref_g), texts(ours), 'ref', 'ours')
    return 'MATCH', ''


def first_diff(left, right, lname, rname):
    for idx in range(max(len(left), len(right))):
        lhs = left[idx] if idx < len(left) else '<end>'
        rhs = right[idx] if idx < len(right) else '<end>'
        if lhs != rhs:
            return 'msg %d: %s=%r %s=%r' % (idx, lname, lhs[:60], rname, rhs[:60])
    return ''


class Known:
    """Divergences accepted for now: lines 'BUCKET :: regex on the key and message :: reason'."""

    def __init__(self, path):
        self.rules = []
        if path and Path(path).is_file():
            for line in Path(path).read_text().splitlines():
                parts = line.split(' :: ')
                if len(parts) >= 2 and not line.startswith('#'):
                    self.rules.append((parts[0], re.compile(parts[1]), parts[2:3] or ['']))

    def reason(self, bucket, key, msg):
        for rule_bucket, regex, reason in self.rules:
            if rule_bucket == bucket and regex.search(key + ' ' + msg):
                return reason[0]
        return None


# ---------------------------------------------------------------- driver


def cache_key(case, tools, parts):
    digest = hashlib.sha1()
    digest.update(json.dumps([case.key, case.std, case.steps, case.top, case.generics,
                              case.stop_time, case.run]).encode())
    for rel in case.files():
        digest.update((case.src / rel).read_bytes() if (case.src / rel).is_file() else b'?')
    for part in parts:
        digest.update(tools.versions[part].encode())
    return digest.hexdigest()


def cached(tools, store, name, case, parts, compute):
    if tools.args.no_cache:
        return compute()
    key = cache_key(case, tools, parts)
    path = store / (case.corpus + '_' + case.name + '.' + name + '.json')
    if path.is_file():
        data = json.loads(path.read_text())
        if data.get('key') == key:
            return data['value']
    value = compute()
    store.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({'key': key, 'value': value}))
    return value


def do_case(case, tools, known):
    work = tools.args.work_dir / case.corpus / case.name
    store = tools.args.cache_dir
    if case.skip:
        return [case.key, 'SKIP', '', case.skip]
    ref_g = cached(tools, store, 'ghdl_an', case, ['ghdl'],
                   lambda: ref_analysis('ghdl', case, work / 'ghdl', tools))
    ref_n = cached(tools, store, 'nvc_an', case, ['nvc'],
                   lambda: ref_analysis('nvc', case, work / 'nvc', tools))
    ours = cached(tools, store, 'ours_an', case, ['ours'],
                  lambda: ours_analysis(case, work / 'ours', tools, ref_g))
    bucket, msg = classify_analysis(ref_g, ref_n, ours)
    ran = ''
    all_ok = all(v[0] == 'ok' for v in ref_g) and all(v[0] == 'ok' for v in ref_n) and all(
        v[0] == 'ok' for v in ours)
    if bucket == 'MATCH' and all_ok and case.run and case.top and tools.args.stage == 'run':
        res_g = cached(tools, store, 'ghdl_run', case, ['ghdl'],
                       lambda: run_ref('ghdl', case, work / 'ghdl', tools))
        res_n = cached(tools, store, 'nvc_run', case, ['nvc'],
                       lambda: run_ref('nvc', case, work / 'nvc', tools))
        res_o = cached(tools, store, 'ours_run', case, ['ours'],
                       lambda: run_ours(case, work / 'ours', tools))
        bucket, msg = compare_run(case, res_g, res_n, res_o)
        ran = 'run'
    elif bucket == 'MATCH':
        ran = 'analysis'
    reason = known.reason(bucket, case.key, msg) if bucket != 'MATCH' else None
    if reason is not None:
        bucket, msg = 'KNOWN', bucket + ': ' + msg + ' (' + reason + ')'
    return [case.key, bucket, ran, msg]


def main():
    doc = (__doc__ or '').split('\n\n', 1)
    parser = argparse.ArgumentParser(description=doc[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=doc[1] if len(doc) > 1 else '')
    parser.add_argument('--nvc-dir', help='NVC checkout (uses test/regress)')
    parser.add_argument('--ghdl-dir', help='GHDL checkout (uses testsuite/gna)')
    parser.add_argument('--no-own', action='store_true', help='skip test_regress/t/t_vhdl_*')
    parser.add_argument('--only', action='append', default=[],
                        help='glob on corpus/name, e.g. nvc/wait* (repeatable)')
    parser.add_argument('--limit', type=int, default=0, help='stop after this many cases')
    parser.add_argument('--stage', choices=['analysis', 'run'], default='run')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--timeout', type=int, default=30, help='seconds per tool command')
    parser.add_argument('--build-timeout', type=int, default=180,
                        help='seconds for the Verilator build of one case')
    parser.add_argument('--work-dir', type=Path, default=Path('/tmp/claude/xcmp/work'))
    parser.add_argument('--cache-dir', type=Path, default=Path('/tmp/claude/xcmp/cache'))
    parser.add_argument('--no-cache', action='store_true')
    parser.add_argument('--known', default=str(Path(__file__).with_name('vhdl_cross_check.known')),
                        help='file of accepted divergences')
    parser.add_argument('--out', help='write a TSV of all results here')
    parser.add_argument('--baseline', help='an earlier --out file: print only what is new')
    parser.add_argument('--summary', action='store_true',
                        help='also list the most common messages per bucket')
    parser.add_argument('--show', default='', help='comma list of buckets to list, default: all'
                        ' but MATCH, KNOWN and SKIP')
    args = parser.parse_args()

    cases = []
    if args.nvc_dir:
        cases += load_nvc(args.nvc_dir)
    if args.ghdl_dir:
        cases += load_ghdl(args.ghdl_dir)
    if not args.no_own:
        cases += load_own()
    if args.only:
        cases = [c for c in cases if any(fnmatch.fnmatch(c.key, pat) for pat in args.only)]
    if args.limit:
        cases = cases[:args.limit]
    tools = Tools(args)
    known = Known(args.known)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(do_case, case, tools, known) for case in cases]
        for done, fut in enumerate(futures, 1):
            try:
                results.append(fut.result())
            except Exception as exc:  # pylint: disable=broad-except
                results.append([cases[done - 1].key, 'CRASH', '', 'script: %r' % exc])
            if done % 200 == 0:
                print('... %d/%d' % (done, len(cases)), file=sys.stderr)

    if args.out:
        Path(args.out).write_text('\n'.join('\t'.join(r) for r in results) + '\n')
    old = {}
    if args.baseline and Path(args.baseline).is_file():
        for line in Path(args.baseline).read_text().splitlines():
            parts = line.split('\t')
            old[parts[0]] = parts[1]
    shown = set(args.show.split(',')) if args.show else set(SEVERITY) - {'MATCH', 'KNOWN', 'SKIP'}
    counts = {}
    for key, bucket, _ran, msg in sorted(results, key=lambda r: (SEVERITY.index(r[1]), r[0])):
        counts[bucket] = counts.get(bucket, 0) + 1
        if bucket in shown and (not args.baseline or old.get(key) != bucket):
            print('%-16s %-40s %s' % (bucket, key, msg))
    if args.summary:
        groups = {}
        for _key, bucket, _ran, msg in results:
            if bucket in shown:
                norm = re.sub(r'/\S+/', '', msg)
                norm = re.sub(r'\d+', 'N', norm)[:100]
                groups[(bucket, norm)] = groups.get((bucket, norm), 0) + 1
        print('\nMost common messages:')
        for (bucket, norm), count in sorted(groups.items(), key=lambda g: -g[1])[:25]:
            print('%5d %-16s %s' % (count, bucket, norm))
    print('\n' + ', '.join('%s %d' % (b, counts[b]) for b in SEVERITY if b in counts))
    return 1 if any(b in counts for b in SEVERITY[:4]) else 0


if __name__ == '__main__':
    sys.exit(main())
