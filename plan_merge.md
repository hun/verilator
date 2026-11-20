<!-- SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
     SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0 -->

# VHDL branch merge plan

Status date: 2026-11-20. Branch: `vhdl-native-frontend`. Mainline base: `53a72711`.

This document is the integration plan for presenting the native VHDL frontend to
the Verilator maintainers. It is a development artifact; it is not user
documentation and should not be linked from `docs/`.

## 1. Independent inout fix

A separate regression fix is already committed outside the VHDL branch:

- Branch: `fix-inout-coalesce`
- Commit: `636884111b` -- `Do not coalesce inout variables`
- Base: `53a72711`
- Files:
  - `src/V3Coalesce.cpp`
  - `test_regress/t/t_tri_inout_top.v`
  - `test_regress/t/t_tri_inout_top.cpp`
  - `test_regress/t/t_tri_inout_top.py`

The fix prevents `V3Coalesce` from replacing reads of an `inout` with its
internal driver. The regression was introduced by mainline commit `5f0d585d`
("Optimize ports of non-inlined instances to improve runtime code sharing");
that commit added `V3Coalesce`, which coalesces `io = io__strong` and loses the
externally driven value.

Evidence:

- `5f0d585d` is bad; its parent `e0231d56` is good.
- `t_tri_inout_top` fails on the bad commit with `y=0 expected 1`.
- The same test passes with the fix.
- All 323 VHDL tests pass with the fix in the VHDL working tree.

These runtime observations come from local test runs; no committed log reproduces
them. Also note `test_regress/t/t_tri_inout_top.cpp:34-36` encodes the OR contract
itself, so the inout decision in §2.2 B governs this test's expectation.

The VHDL branch still carries the same uncommitted change so its local tests
stay green. The clean dependency order is: land the inout fix first, then
rebase or cherry-pick it into the VHDL series.

## 2. Readiness verdict

The VHDL branch is **not ready as one upstream PR**. The core is substantially
better than the branch size suggests, but there are hard CI blockers,
documentation contradictions, and unresolved scope decisions.

Current branch size against `53a72711`:

| Measure | Value |
|---|---:|
| Commits | 363 |
| Files changed | 995 |
| Insertions | ~60.5k |
| Non-test insertions | ~26k |
| Frontend C++ | ~19.7k lines |
| `V3VhdlNativeLower.cpp` | 14,273 lines, one 1,119-line `Lowering` class |
| VHDL test drivers | 323 |
| `.vhdl` sources | 329 |
| C++ harnesses | 227 |
| Golden `.out` files | 75 |
| Test result | 323 passed, 0 failed (local run; not reproducible from tree) |

### 2.1 What is genuinely strong

- Small core integration surface: one language enum, one `V3Global` bool,
  per-file dispatch in `readFiles`, one 45-line change in `V3LinkCells`, and
  build-list entries. Pass order in `Verilator.cpp` is unchanged -- its 69-line
  diff is a scope-block reindent plus one comment, not a pass change.
- No new V3 AST node, pass, or runtime primitive exists only for VHDL. This
  should be the headline of any RFC.
- Layering is real: lexer -> parser/frontend AST -> elaboration/lowering ->
  committed pre-width V3 AST. `V3VhdlParse.cpp` is only a 128-line driver
  (183 lines with its header), not a second parser.
- Tests are meaningful: 227 value-checked C++ harnesses, 75 diagnostic goldens,
  236 `vlt_all` tests. No test invokes GHDL or nvc; the suite needs only the
  built Verilator and a C++ compiler.
- Unsupported constructs are deliberately refused, not silently accepted, at
  238 `unsupportedError` sites (233 in lowering, 2 in lexer, 1 in parser,
  2 elsewhere); 401 total diagnostic call sites.
- The source is hygienic: no TODO/FIXME markers in `src/V3Vhdl*`, no non-ASCII
  in C++.

Verification status (2026-11-20): the counts above were re-measured from the
committed branch except the "Test result" row, which is a local observation.
The scaling timings in §2.2 C are docs-only -- no benchmark script exists.

### 2.2 Hard blockers before a maintainer review

#### A. CI distribution tests (resolved)

Both `dist` checks pass now; the changes are recorded here for the PR:

1. `t_dist_test_style.py:14` whitelist now includes `vhd` and `vhdl`.
2. `t_vhdl_unbound_component_bad.sv` was renamed to `.v`; its driver and golden
   still pass, since extension precedence keeps the module SystemVerilog.
3. The copyright failures came from the author line, not the license. All 883
   VHDL regression sources use Verilator's dual license with `2026 Wilson
   Snyder`; frontend sources, docs, and planning files were normalized to the
   standard author line as well.
4. `t_vhdl_file_init.txt` was renamed to exempt `.dat`, with its VHDL reader
   updated. Header growth shifted line numbers in 72 diagnostic goldens; they
   were corrected, and all 323 VHDL tests pass through `driver.py`.

#### B. Documentation is now truthful

The contradictions were resolved in the shipped docs:

- The inout section no longer promises an OR. It documents one two-state field,
  no IEEE resolution, and the `--pins-inout-enables` interface for external
  drivers. `t_tri_inout_top` still encodes the single-field expectation.
- Refusals are documented as `%Error: ... Unsupported: VHDL ...`; the false
  `%Error-UNSUPPORTED` claim and the non-reproducible corpus classification
  were removed.
- `usage.rst` now uses `design.vhdl` in the `--language` example.
- Mixed-language policy is one contract: an SV top instantiating a VHDL entity
  in one invocation, and a VHDL hierarchy as a `--lib-create` library. A VHDL
  design cannot instantiate an SV module directly.
- The audit opening and table agree: all findings are closed with tests. The
  architecture page no longer claims parse-time dominance or
  independent-simulator confirmation.
- `V3VhdlLink` is described as a pre-width, debug-gated contract.
- Counts are current: 323 drivers, 74 negative drivers (75 goldens), 14,273
  lowering lines. All drivers are git mode `100755`.
- Historical process logs were removed from the PR tree: the root planning
  files, `docs/vhdl/findings.rst`, `journal.rst`, and `vhdl_synth_subset.md`.
  `journal-guide.md` was converted to `.rst` but deliberately left untracked.
  A short user-facing `docs/vhdl/limitations.rst` replaces the subset plan.

#### C. Core integration risks

These are code-review issues independent of the frontend itself:

- `vhdlMode` is global. `--language vhdl` on a `.v` file still parses that file
  as SV; only SV module auto-load (`V3LinkCells.cpp:344-361`) and top-name
  folding (`:44-47`) are disabled globally. Package auto-load is not gated.
- `topModuleName()` lowercases the top whenever VHDL is the default language,
  even for an SV top in a mixed run. Inverse gap: an uppercase VHDL top is not
  folded without `--language vhdl`.
- `STAT_SOURCE_MODULES` is double-counted: the VHDL driver adds it and
  `V3LinkParse` runs unconditionally over the emitted modules.
- `--language vhdl` applies to any unknown or extensionless file.
- Mixed-mode libmap mapping can assign an SV library name to a VHDL `work` file.
- The pre-width contract (`V3VhdlLink`) now runs in every debug build without
  `--debug-check`; `t_vhdl_tree` pins the committed AST of one entity.
- Specialization now uses a typed `SpecKey` (`entity`, `portPart`, `arch`,
  `genericPart`) with explicit coverage predicates; the `entity|` collision is gone.
- One `FileLine` is heap-allocated per token, including discarded and EOF tokens.
- `rootpFromModules()` is removed; `lower()` takes the netlist explicitly.
- Scope snapshots use a copy-on-write signal map; the other ten containers are
  still copied. The old 0.05 s -> 4.70 s probe measured the previous code.

#### D. Test-quality gaps

Not blockers, but they weaken the "independent verification" story:

- Only one test exercises extension-based dispatch; 322 pass `--language vhdl`.
- No `.vhd` test, no `--default-language vhdl` test, no positive test of
  `--language vhdl` on a non-VHDL extension (the `.sv` negative case only proves
  extension precedence).
- Two lint-only tests and two positive compile-only tests;
  `t_vhdl_proc_multiple` checks no values.
- 17 `--binary` tests (15 use `wait for`, 3 assert absolute times), but zero
  pass `--trace*`/`--timing`; only one `--pins-inout-enables` test.
- All 323 VHDL drivers are git mode `100755`; direct invocation works.
- The `vhdl_frontend.flags()` shim was deleted; drivers pass flags directly.
- Duplicate/near-duplicate tests: `t_vhdl_native_cnt` (duplicate of
  `t_vhdl_cnt`), `dyn_slice`/`dynslice`, repeated bare-`wait;` coverage in
  three `wait_*` fixtures. `native_rst` is not a duplicate.
- `nodist/ghdl_synth_suite.py` is packaged but requires an external corpus and
  cannot gate CI.

## 3. How to approach the maintainers

The worst possible move is to open a 60k-line PR cold. The right first artifact
is a short RFC/design issue, not code. It should ask for a scope decision
before review effort.

### 3.1 RFC structure

Title: **RFC: native VHDL frontend for Verilator**

Sections:

1. **Motivation** -- VHDL IP in otherwise-SystemVerilog SoCs; current workflow
   requires an external analyzer or translation; `--language vhdl` should be
   first-class.
2. **Proposal** -- parse synthesizable VHDL-2008 directly, elaborate, lower to
   the existing V3 AST. No new AST node, pass, or runtime primitive. No
   external process/GPL dependency.
3. **Non-goals** -- full VHDL simulation semantics, access/protected/file types,
   user resolution functions, full 9-state resolution, VHDL-2019, general
   timing, fixed/floating point packages, VHDL -> SV binding.
4. **Semantics** -- two-state model, `std_logic` collapse, inout contract,
   report/assert limitations, integer/array/record packing, divergence table.
5. **Architecture** -- one diagram and the pass boundaries. Explicitly state
   that the frontend emits committed pre-width AST before `V3Width`, and why;
   this is the biggest thing maintainers must accept or reject. Name the
   `V3VhdlLink` contract and its current `--debug-check` gating.
6. **Integration** -- extension detection, `--language vhdl`, mixed-language
   stance, build changes, core-file diff size.
7. **Evidence** -- 323 tests, 75 goldens, 236 `vlt_all`, 17 s on 8 cores;
   corpus and real-design sweeps clearly labelled as external/non-reproducible.
8. **Known limitations** -- the open items from `review.rst`: dynamic-index
   driver counts, `std_logic_vector` MULTIDRIVEN, `std_logic_signed`, PSL
   skipping, case-generate, library namespace, zero-width objects,
   BLKANDNBLK/ASSIGNIN false rejects, memory scaling.
9. **Maintenance plan** -- who owns it, how unsupported constructs are triaged,
   how regressions are caught, whether it is always built.
10. **Staged delivery** -- the PR ladder below, and the specific questions
    needed from maintainers.

Questions to ask explicitly:

- Is VHDL in scope for Verilator at all?
- Is emitting a committed pre-width AST acceptable?
- Which mixed-language boundary is required for a first merge?
- Is a staged stack acceptable, or do they want a single feature PR with a
  review guide?
- What CI/performance bar must the first merge clear?
- Who will review the lowering and who maintains it after merge?

### 3.2 Proposed PR ladder

Do not split by file only; split by vertical, testable capability:

| PR | Scope | Why it is reviewable |
|---|---|---|
| **0. Plumbing** | `V3SourceLang`, `.vhd?` detection, `--language vhdl`, per-file dispatch, build lists, docs stub, option/extension tests | Small core change; no parser/lowering yet |
| **1. Lexer/parser/AST** | Design-unit parsing, frontend AST, parse diagnostics, empty-entity lowering to a module | Syntax and diagnostics reviewable independently |
| **2. Flat RTL lowering** | Entities, ports, signals, constants, operators, concurrent assignments, `std_logic`/vectors, minimal `numeric_std` | First useful C++ model; testable end to end |
| **3. Sequential + processes** | Process sensitivity, `if`/`case`, clocks/resets, variables, FSM idioms | Value-checked with C++ harnesses |
| **4. Hierarchy/elaboration** | Instances, components, generics, packages, records, arrays, unconstrained ports | Hardest semantic layer; isolate it |
| **5. Subprograms/stdlib/attributes** | Overload discovery, inlining, `numeric_std` intrinsics, attributes, asserts/reports | Keeps library semantics out of earlier PRs |
| **6. Advanced/timing/tristate** | Waits, delays, inout/tristate, `--pins-inout-enables` | Optional for a minimal merge |
| **7. Mixed language/lib-create** | SV <-> VHDL boundary, `--lib-create` | Defer unless maintainers require it |

PR 0 can be opened immediately after the RFC. Do not open PR 1 until PR 0's
integration shape is accepted.

### 3.3 Mechanical refactor before splitting

Do one refactor PR with no behavior change to make the rest reviewable:

- Done: the lowerer is split into types, expr, stmt, subprog, process, package,
  and elab translation units with shared internal headers.
- Done: `VType` and its helpers live in `V3VhdlNativeTypes.h`.
- Done: the specialization string key is a typed `SpecKey`, and the `entity|`
  coverage collision is fixed.
- Done: the signal map is copy-on-write in scope snapshots.
- Done: `rootpFromModules()` is gone; the netlist is passed explicitly.
- Done: `V3VhdlLink` runs in every debug build, and `t_vhdl_tree` pins one
  entity's committed AST.

### 3.4 Branch shaping

- The inout fix landed first as `600a1d2f`; rebase onto latest `origin/master`
  is still pending.
- Historical and planning artifacts were removed from the PR tree.
- Docs are now `index.rst`, `usage.rst`, `architecture.rst`, `review.rst`,
  `limitations.rst`, and `docs/guide` links. `vhdl_synth_subset.md` was
  replaced by the short `limitations.rst` page.
- CI dist checks are green: whitelist, renamed `.v`/`.dat` fixtures,
  normalized licenses and copyright lines, corrected diagnostic goldens.
- Test permissions are `100755` and the `vhdl_frontend.flags()` indirection
  is deleted.
- Re-run `make test` plus the VHDL suite; document exact command and result.
- Do not edit `Changes` or `docs/CONTRIBUTORS`.

### 3.5 Reviewer package

Give reviewers a one-page guide before the diff:

- What works, with two small VHDL -> C++ examples.
- What is deliberately refused, with two diagnostics.
- The pre-width AST decision and its contract.
- The divergence table (two-state, resolution, reports, timing).
- Test evidence and how to run one test.
- Known limitations and the plan for each.
- A "start reviewing here" map: `V3Global::readFiles`,
  `V3VhdlNative::emit`, `Lowering::emit`, one test.

## 4. Recommended order

1. Land `fix-inout-coalesce` (done on this branch as `600a1d2f`).
2. Fix docs contradictions and test permissions -- done. CI blockers, licenses,
   process-log removal, and the limitations page are in place.
3. Mechanical lowerer split + unconditional contract check -- done.
4. Open the RFC/design issue.
5. Open PR 0 (plumbing) with a green VHDL option/extension test.
6. Build the stacked PRs on top only after maintainers accept the architecture
   and staging.

The branch has enough substance to be taken seriously; the current packaging is
what would sink it. The key is to present it as a synthesizable-subset frontend
with an explicit contract, not as a VHDL simulator and not as a 60k-line dump.
