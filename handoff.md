# VHDL frontend handoff — 2026-11-09 (evening)

Branch: `vhdl-frontend-wp0`
Repo: `/home/hannes/git_extern/verilator`
GHDL: `/home/hannes/.local/bin/ghdl` (7.0.0-dev, llvm 21.1.8)
GHDL source/testsuite: `/home/hannes/git_extern/ghdl`

## Resource limits (do this first)

Always bound long runs, or the machine OOMs:

```sh
ulimit -S -v 8388608         # soft 8 GiB address space per process
make -j4 -C src
test_regress/driver.py -j 4 ...
nodist/ghdl_synth_suite.py --jobs 8 ...
```

A plain `make -j8` build OOM'd the workstation once. Verilator itself is small;
the compilers and corpus fan-out are the risk.

Use `ulimit -S -v` (soft) not a bare `ulimit -v`: a bare call also lowers the
hard limit, after which the shell cannot raise it again (confirmed:
`ulimit -v unlimited` -> Operation not permitted) and ASAN/TSAN tests, which
reserve ~16 TB of virtual address space, can never run in that session. Run
sanitizer or huge-allocation tests in a fresh shell with no `ulimit -v`.

## State

Commits today:

- `bd8f2c8` Fix VHDL contextless logic literals and grouped record aggregates
- `5556440` Remove unused VHDL aggregate type inference
- `c76ea3b` Support VHDL static ranges and aggregate forms
- `1a57ce2` Handle an empty process(all) body when scanning for clock edges
- `052499e` Classify GHDL synth-failure cases in the corpus runner
- `730e2bc` Support VHDL match equality operators
- `2c491d1` Reject clock edges inside process(all)
- `9dfc2bd` Lower VHDL process(all) sensitivity from body reads
- `c08dab1` Lower VHDL multi-clock and wait-until processes
- `42ed440` Add VHDL frontend handoff notes
- `9bf78d4` Tighten VHDL condition and enum semantics, add conversion tests
- `93a5e2c` Flatten VHDL block statements
- `afe54e3` Ignore VHDL package instantiations and PSL clock directives
- `e4c02c8` Support VHDL integer subtype attributes
- `87cedd9` Support VHDL rotate operators, explicit conditions and selected constants
- `1800b47` Support VHDL std_logic_arith conversions
- `d3e2ff6` Support VHDL stdlib vector conversion functions
- `84cbc43` Support VHDL implicit conditions
- `630683c` Support VHDL reduction operators and operator-symbol calls
- `b10c897` Support VHDL aggregate assignment targets
- `115ccf2` Resolve VHDL selected type names and array type definitions
- `5d96727` Normalize artificial open ports for element associations
- `1493f47` Support VHDL element port associations and four-state literals
- `3deb65c` Invalidate VHDL record layouts per specialization context
- `37f0aa6` Support constrained VHDL record subtypes
- `e9a432b` Harden VHDL inout and top-level port handling
- `8b5aab6` Resolve constant prefixes in VHDL names
- `1a0c3b2` Support VHDL unconstrained entity ports
- `c07f322` Support unconstrained VHDL subprogram formals
- `0b6bdd5` Support VHDL positional and expression port associations
- `2631d28` Resolve boolean VHDL generate conditions
- `02bee5a` Use analyzed ranges for nested VHDL names
- `5244b38` Support VHDL non-static slices
- `0779624` Fix VHDL zero-width ranges and record element widths
- `06dc7c7` Support VHDL array aggregates
- `f4ebeea` Reject unmodellable VHDL clock branches
- `3706293` Fix VHDL array element widths
- `84594d2` Support VHDL clock edges in richer conditions
- `c19ff2f` Resolve named VHDL array element types
- `aaa3c9d` Report unbound VHDL instances after dead module removal
- `a94b282` Support VHDL arrays of multi-bit elements
- `f66ba54` Lint the GHDL helper scripts
- `d9f4596` Fix VHDL operator widths and wide literals
- `227b24d` Clean up VHDL diagnostic strings

Working tree: clean at `bd8f2c8` on `vhdl-frontend-wp0`.

Build/test status at stop: `make -j4 -C src` clean; the full `t_vhdl_*` suite
passes (45 tests); the GHDL `synth` corpus was rerun (PASS 665, UNSUPPORTED 300,
ERROR 31, EXPECTED_FAIL 48, GHDL_ERROR 94, NO_TOP 1); `clang-format` clean.
`make lint-py` was not rerun in this session -- the pinned dev tools are not
installed here; the new test scripts pass `python3 -m py_compile`.

**Full suite result:** `make test` cannot start in this environment because the
man-page build needs `help2man` (not installed). The vlt driver was run directly
on the frozen tree (`test_regress/driver.py -j 4 --vlt`, log
`/tmp/full_vlt_final.log`): **4459 passed, 179 env-only failures, 5 skipped,
26:12**, with **zero `t_vhdl_*` failures**. Earlier background runs were
cancelled and no stray driver/verilator processes remain (verified with `ps`),
so there is no cross-run race. Failure breakdown:

- 100 `VCD miscompares` — `wavediff` is not installed (the driver invokes it and
  treats exit 127 as a mismatch); the VCD files are byte-identical to the goldens.
- 61 `lz4.h: No such file or directory` — the FST writer's lz4 dev headers are
  missing.
- 17 ASAN/TSAN failures — the session applied `ulimit -v 8388608` as a *hard*
  limit, and sanitizers reserve ~16 TB of virtual address space.
- 1 `t_threads_crazy_context` warning-as-error (16 hardware threads vs a 1024
  thread request).

`make cppcheck` was not run — `cppcheck` is not installed.

## What works now (VHDL frontend)

Implemented in `src/V3VhdlParse.cpp` on top of the GHDL XML path:

- Entities/architectures, ports (`in`/`out`/`inout`/`buffer`), signals, constants,
  generics (constants and generic type parameters), direct/component
  instantiation, named/positional/bare-expression port associations, writable
  actuals that are names/slices/indices.
- Individual port associations: record members (`i.x => a`) and array elements
  or slices (`v(7 downto 4) => a`, `v(0 to 3) => a`). Individually associated
  unconstrained record/array ports are specialized from GHDL's synthesized
  actual type. Inputs are packed into one concatenation; outputs are driven
  through a per-instance packed temporary and per-element continuous
  assignments. Individual associations of inout ports stay a clean Unsupported.
  GHDL's synthesized type for an unconstrained array is ascending `0 to N-1`,
  which is also the elaborated index range; descending slice formals on an
  unconstrained port are not elaborated by GHDL.
- Unconstrained entity ports (arrays and records) are specialized per
  instantiation from the actuals (named/positional/slice actuals, declared port
  defaults, GHDL artificial opens). Record port layouts are tracked per
  specialized port variable, the specialization key includes each record's
  member name/lsb/width, and the node-keyed record layout cache is only used
  outside any generic/generate binding context. Equal-width records with
  different member layouts therefore get distinct modules. An unconstrained-port
  entity selected as the top has no single width and is reported as no-top
  rather than given an invented size.
- Unbound component instances are reported after dead-module removal; VHDL mode
  never auto-loads SystemVerilog sources.
- Generic package instantiation declarations, implicit attribute declarations and
  PSL default-clock directives are accepted and ignored; PSL assertion/assume
  directives remain unsupported rather than silently dropped.
- Aggregate assignment targets (`(a, b) <= value`), for concurrent signals and
  variables: the value is split left to right, first target most significant.
- Block statements are flattened with a unique scope suffix; local signal
  declarations and nested concurrent statements/generates are supported.
  Blocks with a port/generic/guard header remain a clean Unsupported.
- Concurrent and selected assignments, clocked/sync/async-reset processes,
  `if`/`elsif`/`else`, `case`, static `for` loops, for/if-generate (booleans and
  negated boolean generics included).
- Clock idioms: `rising_edge`/`falling_edge` and `sig'event [and sig =/= '0'/'1']`,
  composed of `and`/parentheses with level enables, in if/elsif chains with
  enable/reset branches, including concurrent conditional clocked assignments.
  Rejected cleanly when the model would lose behaviour: complex asynchronous
  resets and clock edges on signals outside the sensitivity list.
- Processes whose sensitivity list is a single signal are lowered as one
  `always` on that signal and clock edges become the signal's level, which is
  exact because the process only runs on that signal's events.  This keeps
  later/`else` branches, multiple edges (`rising_edge`/`falling_edge` in one
  chain) and process variables in source order.
- Processes with several clocked statements, or clocked statements mixed with
  unclocked ones, are split into one `always` per distinct clock sensitivity
  plus one for the unclocked remainder when process variables and assignment
  targets are provably disjoint; otherwise they are a clean Unsupported.
- Wait-based processes with a top-level `wait until <clock edge>` (and any
  statements before it, which the process loop re-executes after each wakeup)
  become an edge-triggered `always` plus a time-zero initial.
- `process(all)` becomes one `always` whose ET_CHANGED sensitivity is every
  signal the lowered body reads (process variables excluded), plus a time-zero
  initial.  A clock edge inside `process(all)` remains unsupported.
- Types: `std_logic(_vector)`, `std_ulogic(_vector)`, `bit(_vector)`,
  `unsigned`/`signed`, integers/subtypes, booleans, enums, records (including
  package-qualified `work.pkg.type` marks and unconstrained array types used as
  function formal/return types), constrained record subtypes with VHDL-2008
  element constraints (including layered subtypes and records with
  unconstrained members), constrained 1-D arrays with builtin or named element
  types, nested arrays, arrays of vectors
  and records, aliases (including aliases of slices).
- Arrays are packed into one flat vector with `elemWidth` bits per element.
  Indexing selects whole elements, slices keep element width, attributes
  (`'left`/`'right`/`'high`/`'low`/`'length`/`'range`) use element indices, and
  aggregates support positional, named, range and `others` choices.
  Ascending arrays use the rightmost element as the least significant.
- String generics: the actual (or default) is a string literal, bound per
  specialization and included in the specialization key. The value is an
  array of 8-bit `character` enum elements indexed `1 to length`, so `'low`/
  `'high`/`'left`/`'right`/`'length`/`'ascending` work, indexing yields the
  character dtype, and case choices compare character enum positions.
  Positional and named generic associations are both supported. Type
  attributes over `subtype T is natural range arr'range` resolve through the
  array's range as well.
- Non-static slices are supported when the width is statically known
  (`mem(a*8+7 downto a*8)`), including writes.
- Static elaboration expressions: integer division, modulus, remainder,
  exponentiation and `abs` in ranges; `'range` used as a constraint; aggregates
  whose GHDL-determined type supplies their bounds (qualified element choices
  and range-attribute choices); shared `0 | 1 => value` alternatives in array
  and record aggregates; positional
  associations that span several elements (`("01", "10")`); and function-local
  aggregate assignments constrained by the target variable's type.
- Expressions: arithmetic/comparison/logic/shift/rotate/concat, static
  attributes (including `'left`/`'right`/`'high`/`'low` on integer subtypes),
  type conversions, `numeric_std` conversions, qualified
  expressions, bit-string literals of any width, array aggregates. Unary
  reductions (`and`/`or`/`xor` and their negations), `std_logic_misc`
  `*_reduce` functions and VHDL-2008 operator-symbol calls such as `"or"(a)`.
  Implicit conditions on scalar logic values (`if s then`), the identity
  `to_stdlogicvector`/`to_stdulogicvector`/`to_slv`/`to_bitvector`/
  `to_stdulogic` conversions, the two-argument
  `numeric_std_unsigned.to_*vector(value, length)` form, and
  `std_logic_arith.conv_std_logic_vector`/`conv_unsigned`/`conv_signed`.
  Rotate operators (`rol`/`ror`), unary `+`, explicit `??` conditions,
  redundant parentheses, and package-qualified constants
  (`work.pkg.constant`).  VHDL-2008 matching equality/inequality (`?=`, `?/=`)
  with `'-'` don't-care positions in a literal; without a wildcard literal it is
  same-value (`===`) equality.  Matching relational operators (`?<` and
  friends) and `'Z'` in a match literal remain unsupported.
- Four-state logic literals: `'X'`, `'U'`, `'W'`, `'-'` map to unknown and
  `'L'`/`'H'` to 0/1, in character and string literals. `'Z'` stays a clean
  Unsupported because driving high impedance reaches the unresolved
  V3Tristate/DFG limitation (see item 5).
- `numeric_std` result lengths, mixed-width comparisons, bit-by-bit literals,
  zero-width (null) arrays/slices rejected cleanly.
- User-defined pure functions and procedures, inlined; unconstrained formal
  parameters take the actual's type, parameter-dependent local bounds and
  attributes on formals work. Function-local variable *element* assignment is
  rejected (locals are bound to values).
- Recursion is unsupported (explicit diagnostic).

Tests:

- `t_vhdl_arith_conv` — `std_logic_arith` `conv_std_logic_vector`/
  `conv_unsigned`/`conv_signed` sizing (run with `-fsynopsys`).
- `t_vhdl_conv` — conversions, widths, literals (including four-state
  `'L'`/`'H'`/`'X'`/`'-'`), aggregates, aggregate assignment targets, reduction
  operators and operator-symbol calls, implicit conditions, stdlib vector
  conversions, an unconstrained `std_ulogic_vector` function return type.
- `t_vhdl_operators` — integer and vector operators incl. mixed widths, rotates,
  unary `+`, explicit conditions, package-qualified constants and integer
  subtype attributes.
- `t_vhdl_array` — arrays of vectors, nested arrays, records, enums, aggregates,
  slices, attributes, `'range`.
- `t_vhdl_string_generic` — string generics at different lengths and contents,
  positional and named generic associations, `'low`/`'high`/`'length`, dynamic
  indexing, and character case choices.
- `t_vhdl_slice` — non-static slices (read and write), ascending order.
- `t_vhdl_event` — clock idioms, level gates, `'event` forms, async reset,
  concurrent clocked assignment.
- `t_vhdl_split` — two same-edge statement chains that share a target, a clocked
  statement mixed with an unclocked one that reads the clocked register
  (`dff04` pattern), two clocks with disjoint targets and disjoint process
  variables, a later branch that runs on the falling edge (and both edges in
  one chain), and a variable carried across statements.
- `t_vhdl_wait` — `wait until` on rising/falling edges and the `'event` form,
  including the loop-back execution of statements before the wait and a
  variable initialized before it.
- `t_vhdl_process_all` — process(all) with a variable, if/else and output
  updates on input changes without a clock.
- `t_vhdl_match` — matching equality/inequality with don't-care positions,
  wildcards in the middle and same-value matching without wildcards.
- `t_vhdl_clock_bad` — rejected clock chains (later branch with a multi-signal
  sensitivity list, complex asynchronous reset, different clocks sharing a
  target reached through the second statement of a merged same-edge group, a
  clock edge in process(all)).
- `t_vhdl_unconst_port` — unconstrained entity ports at several widths, slices,
  defaults, component binding.
- `t_vhdl_record_sub` — constrained record subtypes, layered constraints,
  aggregates, member access, an unconstrained record port declared through a
  package-qualified type name, specialized from the actual. Two child instances
  use equal-width records with different member layouts and select a member
  slice (`i.x(3 downto 0)`), which fails if the specialization key or layout
  lookup collapses them.
- `t_vhdl_record_ind` — individual record port associations for unconstrained
  and constrained records, input expressions and output member driving. Also
  covers an unassociated child port whose name equals a record member, which
  exercises artificial-open port resolution.
- `t_vhdl_port_slice` — individual slice associations on constrained and
  unconstrained array ports, read and write directions.
- `t_vhdl_function`, `t_vhdl_procedure` — inlining, unconstrained formals,
  nested capture.
- `t_vhdl_alias`, `t_vhdl_ifgen`, `t_vhdl_gen_inst`, `t_vhdl_gen` (also a generic
  package instantiation and a block with a local signal), `t_vhdl_event` (also a
  PSL default clock),
  `t_vhdl_unbound_component_bad` and the rest of `t_vhdl_*`.

## GHDL integration tooling

- `nodist/ghdl_xml` — dumps the analyzed GHDL XML consumed by the frontend.
- `nodist/ghdl_synth_suite.py` — lints GHDL's `synth` testsuite through
  `--language vhdl --lint-only`, categorizes results, writes a TSV report.
  Files the test's `testsuite.sh` marks with `synth_failure` are categorized
  `EXPECTED_FAIL` when the frontend rejects them and `PASS` when it accepts
  them, so negative tests do not inflate the `ERROR` bucket.
  - Default testsuite: `$GHDL_TESTSUITE` or `../ghdl/testsuite`.
  - Parses `GHDL_STD_FLAGS`/`GHDL_FLAGS` from each test's `testsuite.sh`.
  - Logs/report: `obj_dir/ghdl_suite/synth/` (gitignored).
- `VERILATOR_GHDL_FLAGS` — extra GHDL analysis flags appended after `--std=08`.
- Both scripts are in `PY_PROGRAMS` and lint clean (pinned `ruff==0.14.8`,
  `pylint==4.0.9`, `mypy==1.19.0`; `make venv` per `docs/guide/install.rst`).

### Baseline (`synth`, 1139 scripted cases)

| status | count |
|---|---|
| PASS | 665 |
| UNSUPPORTED | 300 |
| EXPECTED_FAIL | 48 |
| GHDL_ERROR | 94 |
| ERROR | 31 |
| NO_TOP | 1 |

Progress across the day's states (original / after width fixes / now):
PASS 369 -> 360 -> 665; UNSUPPORTED 622 -> 642 -> 300;
ERROR 38 -> 27 -> 31; GHDL_ERROR 107 -> 94; EXPECTED_FAIL 50 -> 48; NO_TOP 3 -> 1.
`EXPECTED_FAIL` was split out of the older `ERROR`/`UNSUPPORTED`/`GHDL_ERROR`
buckets for files the testsuite marks `synth_failure`, so the ERROR comparison
is not apples-to-apples.  Net vs original: +296 PASS, -322 UNSUPPORTED,
-7 ERROR. The element-port step
added `iassoc04`/`riassoc04` and X/weak logic literals (`toint02`, `toint03`,
and related cases); the type-name step added the `issue2826`/`issue2834`
package-qualified record designs and `issue1081`; aggregate targets added
`issue1178`, `issue2279`, `aggr02/targ04` and several `issue1428` cases;
reductions added `unary01`, `issue1140`, `issue2224`, `issue2328`, `sns01/reduce`
and the `outvlg/oper*` cases. Unconstrained-port
entities selected as tops now report "unconstrained port on a top-level entity"
instead of becoming NO_TOP; procedural assignments to inout ports report a clean
Unsupported instead of internal errors. The ERROR count now
mostly consists of real design errors (missing modules/entities, unknown
identifiers, out-of-range constants) and 6 pre-existing internal faults
(`V3DfgSynthesize` width/read-only, `V3Tristate`), not new crashes.

Main remaining UNSUPPORTED buckets:

- waits without a top-level clock edge (44; `wait on`, `wait for`, waits inside
  loops)
- PSL assertion/assume directives (21) — verification only, ignored by
  synthesis but rejected rather than dropped.  The string-generic `issue` tops
  now lower fully and stop only at these directives; the two standalone
  `sequencer` tops stop at the top-level unbound-generic diagnostic, and
  `issue1850/issue` still stops at its unsupported `process_statement`.
- generic map on units with no VHDL entity (14; the runner analyses one top
  file and cross-language binding is WP3)
- non-static slice bounds and multi-dimensional arrays (10 each)
- generic without a default on the selected top entity (10) — previously these
  passed lint-only as body-less base modules; the frontend now rejects them
  instead of silently emitting an empty top.
- indexing a non-vector name (10)
- character literals `'Z'` (9) and string-literal characters (8)
- process variables shared by clocked and unclocked statements (8) and null
  arrays (8; intentionally zero-width designs)
- non-static range limits (7; pure function calls and variable-derived bounds)
- `external_signal_name` (7)
- unsupported library/user function calls: `ffs` (6), `get_val` (4), `is_x`
  (3), `init_ram` (3) and other numeric/file helpers
- asynchronous reset conditions (6) and array element type `integer` (6)
- unconstrained port on a top-level entity (5), `exit_statement` (5)
- interface view ports (3), non-static `resize` widths (3), assignment to a
  variable element in an inlined function (3)
- `floating_point_literal` (3) and `event_attribute` on non-sensitivity names
  (3)

Clock edges inside `process(all)` also remain unsupported.

ERROR buckets: 15 unresolved instances (`Cannot find VHDL entity or module`;
two more surfaced now that more architectures elaborate), 4 entities without
architectures, 4 unknown identifiers, 1 string-literal length mismatch,
and 6 pre-existing internal faults (4 `V3DfgSynthesize` width
mismatches; 2 `Non-ReadOnly reference` from inout-port-to-pin nets, e.g.
`issue1387/repro2`, `issue3225/tb`).  The net ERROR 30 -> 31 is unmasking, not
new breakage: `issue3175/t` now reaches its unbound generic (previously stopped
earlier at an array element type), and `issue1095/top` reaches its unresolved
configuration-bound component; `issue1117/ent` moved ERROR -> PASS.

## Next steps (remaining handoff todos)

1. Cross-language binding (WP3): component → SystemVerilog module lookup and
   configuration declarations. Mixed VHDL/SV invocations are still rejected and
   VHDL mode never auto-loads `.v`; unresolved instances are clean errors.
   This is a separate milestone, not a small follow-up.
2. Clocked processes that remain rejected: multiple sensitivity signals with
   process variables shared between clocked and unclocked statements
   (`dff14`-style), different clocks sharing a variable (`mem02/dpram4`), and
   clocks not in the sensitivity list.  Splitting these is only safe with a
   real liveness/ordering analysis.
3. Waits that are not a top-level clock edge (inside loops, `wait on`,
   `wait for`) and clock edges inside `process(all)` — the remaining
   process-form gaps.
4. Inout ports connected to inout pins can still hit a core DFG assertion
   (V3Tristate-generated `__Vcellinpt__` chain used as an RHS); needs core work
   or a broader frontend rejection. The same limitation keeps `'Z'` literals
   and individual associations of inout ports unsupported.
5. Remaining non-logic character/string literal contexts (`'a'`, `'Z'`,
   characters used as an enum type rather than as logic values), and
   matching relational operators (`?<`/`?<=`/`?>`/`?>=`).
6. Function-local variable element assignment inside inlined functions (deep
   value rewrite; currently a clean `Unsupported`).
7. In an environment with `help2man`, lz4 headers, `wavediff` and without a
   hard virtual-memory cap, rerun `make test` for a clean full-suite result.
   Run `make cppcheck` where installed.

## Gotchas

- Set `ulimit -v 8388608` and cap `-j`/`--jobs` as above.
- A VHDL array is packed flat with no dtype range of its own; anything that needs
  VHDL indices or element width must recover it with `arraySpec()`/`multiArraySpec()`
  from the analyzed type. Ascending arrays put the rightmost index at the LSB.
- An entity base module is populated only when every constant generic has a
  default; a generic without one has no single body, so only its
  specializations emit.  Populating a base module binds its string generic
  defaults; integer defaults keep resolving through the declaration as before.
- Whole string generics passed to an unconstrained-array/`string` subprogram
  formal lose their VHDL layout: the formal is a flat 8*length logic vector, so
  indexing inside the function selects one bit and attributes use the bit
  width.  Only direct indexing/attributes on the generic name are supported.
- Function locals are bound to expressions, not `AstVar`s: only simple-name
  assignments update them; constants bound via inlining are visible to
  `evalConst` through `m_inlineEnvp`.
- Clock chains: with a single sensitivity signal, later branches, `else` and both
  edges in one chain are lowered exactly (the edge becomes the signal's level).
  Otherwise the clock branch must be last, branches before it on a
  sensitivity-list signal must be simple level resets, and an `else` is rejected.
- Process splitting only groups same-sensitivity statements; different clock
  groups and the unclocked remainder must have disjoint assignment targets and
  no shared process variables, else the process stays a clean Unsupported.
- `emitSingleClockProcess` runs before `emitClockedIf` whenever the fast path
  does not apply; a single top-level clocked chain still takes the edge-sensitivity
  fast path.
- `process(all)` sensitivity is inferred from the read references of the
  lowered body (process variables excluded), so it is recomputed after every
  lowering; a body that reads no signal gets only the time-zero initial.
- Match operators build the don't-care mask from the XML literal text, because
  lowering `'-'` and `'X'` both become an unknown bit; `'-'` is a wildcard,
  `'X'` is a value.  Unknown data bits compare via case equality, so only the
  documented 2-state collapse differs from full VHDL matching.
- A `std_logic`/`std_ulogic` literal with no context lowers to one logic bit;
  `isLogicEnum` detects the nine-literal IEEE types. GHDL's enum dtype for them
  is 4 bits, which would otherwise skew concatenations and element aggregates.
  User enums keep their enum dtype.
- VHDL mode skips `V3LinkDot`, so `V3LinkCells` defers unresolved cells with
  `AstNotFoundModule`, and `Verilator.cpp` reports surviving ones after
  `V3Dead::deadifyModules`; unreachable instances still pass.
- Run the regression suite from `test_regress` with shell-expanded `.py` paths
  and an explicit root: `cd test_regress && VERILATOR_ROOT=$PWD/.. ./driver.py
  -j 4 --vlt t/t_vhdl_*.py`. Invoking `driver.py` from the repository root with
  `test_regress/t/...` paths reported "Can't locate dir" in this session.
- Element associations use GHDL's marker node for the specialized actual type and
  the per-element by-name entries for the parts. GHDL's synthesized type for an
  unconstrained array is ascending `0 to N-1`; valid slice formals therefore use
  `to`, while descending formals are rejected at GHDL elaboration.
- Reusing `test_regress/obj_vlt/<test>` after adding ports can leave a stale
  generated header (`no member named ...`). Remove the obj dir and rerun.
- `ghdl --file-to-xml` is invoked with `--std=08`; `VERILATOR_GHDL_FLAGS` is appended
  after it, so a later `--std=...` wins. Synopsys packages such as
  `std_logic_arith` need `-fsynopsys`, set by `t_vhdl_arith_conv`.
- The GHDL runner is lint-only triage. It does not simulate testbenches, and
  `--include-testbenches` only lints them; `assert`/`report` are ignored by design.
- The GHDL testsuite has expected-failure tests (`synth_failure`); they show up as
  `GHDL_ERROR`, not as regressions.
- Do not edit `Changes`; maintainers update it near release.
