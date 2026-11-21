<!-- SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
     SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0 -->

# VHDL branch merge plan

Branch: `vhdl-native-frontend`. Mainline base: `53a72711`.

This document is the integration plan for presenting the native VHDL frontend to
the Verilator maintainers. It is a development artifact; it is not user
documentation and should not be linked from `docs/`.

## 1. Independent inout fix

The inout regression fix landed in this branch as commit `600a1d2f`:

- Commit: `600a1d2f` -- `Do not coalesce inout variables`
- Original standalone branch: `fix-inout-coalesce`, based on `53a72711`
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
- All 324 VHDL drivers pass with the fix in this tree.

These runtime observations come from local test runs; no committed log reproduces
them. Also note `test_regress/t/t_tri_inout_top.cpp:36-38` still expects the OR
of the external value and the enabled internal driver, while
`docs/vhdl/limitations.rst` promises no guaranteed OR; that expectation needs to
be reconciled.

The fix is already part of the branch series; only the rebase onto the latest
`origin/master` remains (section 3.4).

## 2. Branch state

The core integration surface is small; the branch is large, and the merge still
depends on scope decisions from the maintainers. The outstanding issues are
listed below.

| Measure | Value |
|---|---:|
| Commits | 383 |
| Files changed | 1006 |
| Insertions | ~58.3k |
| Non-test insertions | ~23.0k |
| Frontend C++ | ~20.9k lines (`V3Vhdl*`) |
| Lowering units | Seven `V3VhdlNativeLower*.cpp` files plus `V3VhdlNativeLowerImpl.h` and `V3VhdlNativeTypes.h` |
| VHDL test drivers | 329 |
| `.vhdl` sources | 329 |
| C++ harnesses | 227 |
| Golden `.out` files | 76 |
| Test result | 329 passed, 0 failed (local run; not reproducible from tree) |

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
- Tests are meaningful: 227 value-checked C++ harnesses, 76 golden files,
  237 `vlt_all` tests. No test invokes GHDL or nvc; the suite needs only the
  built Verilator and a C++ compiler.
- Unsupported constructs are deliberately refused, not silently accepted, at
  237 `unsupportedError` sites; 162 direct `v3error` call sites.
- The source is hygienic: no outstanding work markers in `src/V3Vhdl*`, no non-ASCII
  in C++.

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
   were corrected, and all 324 VHDL tests pass through `driver.py`.

#### B. Documentation is now truthful

The contradictions were resolved in the shipped docs:

- The inout section no longer promises an OR. It documents one two-state field,
  no IEEE 1164-1993 resolution, and the `--pins-inout-enables` interface for
  external drivers. `t_tri_inout_top` still expects an OR, so that test needs to
  be reconciled with the documented contract.
- Refusals print `%Error-UNSUPPORTED: ... Unsupported: VHDL ...` at the
  token's line and column, like every other Verilator unsupported construct.
  The non-reproducible corpus classification was removed.
- `usage.rst` now uses `design.vhdl` in the `--language` example.
- Mixed-language policy is one contract: an SV top instantiating a VHDL entity
  in one invocation, and a VHDL hierarchy as a `--lib-create` library. A VHDL
  design cannot instantiate an SV module directly.
- Known limitations are listed in `docs/vhdl/limitations.rst` under "Known
  limitations"; there is no audit page in the branch.
- Counts are current: 329 drivers, 76 negative drivers (77 goldens). The
  lowerer is split across seven translation units plus two headers. All drivers
  are git mode `100755`.
- Historical process logs were removed from the PR tree: the root planning
  files, `docs/vhdl/findings.rst`, `journal.rst`, and `vhdl_synth_subset.md`.
  `journal-guide.rst` lives outside the repository.
  A short user-facing `docs/vhdl/limitations.rst` replaces the subset plan.

#### C. Core integration risks

These are code-review issues independent of the frontend itself:

- `vhdlMode` is global: any VHDL source disables SV module auto-load
  (`V3LinkCells::resolveModule`) for the whole run, so a mixed design that
  relies on `-y` breaks. Package auto-load is not gated. Deciding per
  instantiating module would fix both.
- Resolved: `--top-module` folds to lowercase whenever VHDL sources are
  present unless a module matches exactly, so an SV top in a mixed run keeps
  its case and `.vhd` files need no `--language vhdl`.
- Resolved: `STAT_SOURCE_MODULES` is counted once, by `V3LinkParse`.
- `--language vhdl` applies to extensionless files and extensions no option
  maps; `.v`, `.sv`, `.vh`, `.svh` and `+<lang>ext+` mappings stay SV.
- Mixed-mode libmap mapping can assign an SV library name to a VHDL `work` file.
- Specialization now uses a typed `SpecKey` (`entity`, `portPart`, `arch`,
  `genericPart`) with explicit coverage predicates; the `entity|` collision is gone.
- One `FileLine` is heap-allocated per token, including discarded and EOF tokens.
- `rootpFromModules()` is removed; `lower()` takes the netlist explicitly.
- Scope snapshots use a copy-on-write signal map; the other ten containers are
  still copied. The old 0.05 s -> 4.70 s probe measured the previous code.

#### D. Test-quality gaps

Not blockers, but they weaken the "independent verification" story:

- Few tests exercise extension-based dispatch; most pass `--language vhdl`.
- No `.vhd` test and no `--default-language vhdl` test. `t_vhdl_lang_ext`
  checks that `--language vhdl` keeps a `.vh` header SystemVerilog.
- Two lint-only tests and two positive compile-only tests;
  `t_vhdl_proc_multiple` checks no values.
- 18 `--binary` tests (16 use `wait for`); `t_vhdl_timescale` checks delays
  under `--timescale`. `t_vhdl_trace` is the only `--trace*` test; none pass
  `--timing`; only one `--pins-inout-enables` test.
- All 329 VHDL drivers are git mode `100755`; direct invocation works.
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
5. **Architecture** -- one diagram and the pass boundaries: parser and frontend
   AST, elaboration and lowering, then the unchanged V3 passes.
6. **Integration** -- extension detection, `--language vhdl`, mixed-language
   stance, build changes, core-file diff size.
7. **Evidence** -- 329 drivers, 77 goldens, 237 `vlt_all`, ~15 s on 16 cores;
   corpus and real-design sweeps clearly labelled as external/non-reproducible.
8. **Known limitations** -- the items listed in `docs/vhdl/limitations.rst`:
   imprecise dynamic-index driver counts and `std_logic_vector` MULTIDRIVEN
   diagnostics, unmodelled `std_logic_signed` arithmetic, skipped PSL, missing
   `case generate` and matching `case`, simplified library namespaces,
   BLKANDNBLK/ASSIGNIN false rejects, and memory scaling on large designs.
9. **Maintenance plan** -- who owns it, how unsupported constructs are triaged,
   how regressions are caught, whether it is always built.
10. **Staged delivery** -- the PR ladder below, and the specific questions
    needed from maintainers.

Questions to ask explicitly:

- Is VHDL in scope for Verilator at all?
- Is the committed V3 AST contract acceptable?
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
- Docs are now `index.rst`, `usage.rst`, `architecture.rst`, and
  `limitations.rst`, plus the `docs/guide` links. `vhdl_synth_subset.md` was
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
- The AST the frontend commits to the V3 passes and its contract.
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
4. RFC/design issue: draft prepared at `rfc_native_vhdl_frontend.rst`; posting
   awaits maintainer-facing cleanup and a human in the loop.
5. Open PR 0 (plumbing) with a green VHDL option/extension test.
6. Build the stacked PRs on top only after maintainers accept the architecture
   and staging.

The key is to present it as a synthesizable-subset frontend with an explicit
contract, not as a VHDL simulator and not as an undifferentiated code dump.
