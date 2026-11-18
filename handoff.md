# VHDL frontend handoff -- 2026-11-18, end of the conformance session

**Purpose.** The VHDL conformance work from `docs/vhdl/findings.rst` is **done**:
all 28 findings (N99..N126) are closed, and the VHDL regression suite is green.
This file records the final state, what was changed, and how to verify it.

## State

- Branch `vhdl-native-frontend`.
- Suite: `test_regress/driver.py --vlt 't/t_vhdl_*.py'` -> **239 tests, 0
  failures**. Each closed finding has its test; each new diagnostic has its
  `_bad`/`_unsup` golden.
- `docs/vhdl/findings.rst`: the Closed table carries every finding (N99..N126)
  with its resolution and test name. The Open table keeps only the two findings
  a frontend-only change cannot close: **N15** (needs `AstModule::isTop()` from
  the middle end) and **N67** (the simulation-only subset).
- `vhdl_synth_subset.md` (repo root, planning doc): tier table refreshed against
  the shipped frontend; zero initialization of uninitialized objects documented
  next to the logic-value collapse (5.1); `boolean'image` is lower case
  (IEEE 1076-2008 16.2.2) and `t_vhdl_image_attr` is re-pinned.

## What the conformance review changed (one commit per finding)

Value fixes (lowering): N99 scalar character literal vs. vector element-wise
logic; N100 qualified scalar keeps its type; N101 don't-cares through a
constant wildcard `std_match`/`?=`; N102 negative shift counts shift the other
way; N103 a runtime loop bound is captured at entry; N104 time-zero signal
writes are non-blocking; N105/N106 `process(all)` sees target indices and
entity-declared signals; N107 a bare `wait;` suspends for good; N108/N109
severity failure ends the run at the statement and a logic-character report
message prints; N111 `abs` on `signed`.

Parser/acceptance: N110 legal forms (based literal exponent, positional
`open`, no-map instantiation, `for i in <subtype> generate`, `view` as an
identifier, single-declaration `use`, `'('` literal). Diagnostics: N112 case
and selected choice completeness/duplicates; N113 logical operator chains need
parentheses; N114 identifier spelling and reserved words; N115 unresolved
signal driver count per bit position; N116 association lists cannot mix forms;
N117 port actual conformance; N118 integer literal and constant subtype
ranges; N119 `std_logic_vector` arithmetic needs a package (shifts stay legal:
`std_logic_1164`-2008 declares them); N120 run-time physical objects refused;
N121 wait in a sensitized process refused; N122 non-bare sensitivity items
refused; N123 `'val` bounds and the predefined scalar attribute set; N124
division by zero (static error, dynamic run-time check).

Docs: N125 zero initialization documented; N126 tier table refreshed.

## Verify

```sh
ulimit -S -v 8388608                 # soft limit; a bare `ulimit -v` breaks ASAN/TSAN
cd /home/hannes/git_extern/verilator-native
make -j4 -C src                      # absolute path; `-C src` elsewhere silently fails
cd test_regress
VERILATOR_ROOT=/home/hannes/git_extern/verilator-native \
  timeout 3000 python3 driver.py -j 4 --vlt t/t_vhdl_*.py
```

The glob must be **unquoted** so bash expands it; driver.py runs from
`test_regress`. One test: `python3 t/t_vhdl_<name>.py`.

## Environment notes

- `make format` fails (no `gersemi`); format only changed files with
  `clang-format-21 -i <files>`. Repo-wide clang-format touches four unrelated
  tracked test `.cpp` files (version drift) -- revert those.
- `help2man`, `lz4`, `wavediff` are absent; `make test` cannot complete here.
- New diagnostics use `v3error`/`v3warn` (repo `AGENTS.md` table) and need a
  `_bad`/`_unsup` test whose `.out` golden is produced with
  `HARNESS_UPDATE_GOLDEN=1 python3 t/<name>.py` (touch the `.out` first).
- Commits go through the eval `commit` namespace; `d.take(path)` takes one
  path per call. Do not edit `docs/CONTRIBUTORS` or `Changes`; a human posts
  the PR.
- GHDL (`/home/hannes/.local/bin/ghdl -a --std=08`) and nvc are the reference
  simulators. GHDL confirms an illegal-identifier test: `signal bus` is a
  reserved word (the two inout tests were corrected).
