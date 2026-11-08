<!-- SPDX-FileCopyrightText: 2026 Hannes Klas
     SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0 -->

# VHDL frontend for Verilator — implementation plan

**Status:** proposal; WP0 plumbing implemented on branch `vhdl-frontend-wp0` (commits
`729df7a`, `bed81f2`). Nothing beyond WP0 is implemented. Not user documentation; this is
a planning document and must not be linked from `docs/`.

**Target tree:** Verilator 5.055 devel, commit `9c28d84` (2026-11-07).

**One-line verdict:** reusable backend, replaceable front half. A synthesizable VHDL
subset is 25–45k new LOC (1.5–3 engineer-years for a solid subset, 5–10 person-months
for a demo subset). Faithful VHDL-2008 simulation semantics are not reachable without a
new value model and are out of scope.

## Implementation status (2026-11-08)

| Work package | State |
|---|---|
| WP0 plumbing | **done** on branch `vhdl-frontend-wp0` (`729df7a`, `bed81f2`) |
| WP1 front end | **GHDL XML path implemented**: entities with ports, architecture signals, concurrent assignments, clocked processes (`rising_edge`/`falling_edge`/`clk'event and clk = '1'`), synchronous and asynchronous reset processes, conditional and selected concurrent assignments, `if`/`elsif`/`else`, `case`, process variables, indexing and slicing, `and`/`or`/`xor`/`not`, `+`/`-`, `=`/`/=`, type conversions, `to_unsigned`/`to_signed`/`resize`/`to_integer`, declaration initializers, and component/direct-entity instantiation with named, positional and open port maps. Component declarations are validated against the bound entity (direction and width). Processes execute once at time zero, as VHDL requires. The in-tree lexer/parser remains reserved and is not used |
| WP2 semantics | provided by GHDL analysis; Verilator does not repeat it on this path |
| WP3 elaboration | entity/architecture binding, VHDL-entity instance binding and component-interface validation; generics and generate are not implemented |
| WP4–WP8 | not started |

The active frontend runs `ghdl --file-to-xml --std=08` out of process, parses the XML into a
minimal DOM (`V3VhdlXml`), and lowers the analyzed design units to a post-width-commit
Verilator AST (`V3VhdlParse::finishXml`). GHDL is therefore a build-time dependency of VHDL
runs; `VERILATOR_GHDL` selects the executable. The GPL boundary stays at the process
boundary.

Supported expression forms are those the lowering recognizes explicitly; everything else
reports "not supported yet" with the VHDL source location. The in-tree parser is kept for
the day the front end must be self-contained again; it is not reachable from
`--language vhdl` today.

| Work package | State |
|---|---|
| WP1 parser (in-tree) | reserved; not started |
| WP2–WP8 | not started |

WP0 proves the seam end-to-end:

- `.vhd`/`.vhdl` detection and `--language vhdl`; a later `--language <IEEE 1800>`
  restores SystemVerilog mode, so option order works both ways. Explicit file extensions
  always win over the default.
- Per-file `V3VhdlParse::parseFile()` collects design units only; `finish()` binds
  `(library, entity)` to architectures and emits `AstModule`s with port `AstVar`s before
  `V3LinkCells`.
- `V3VhdlLink` applies the `.vlt` directives that `V3LinkParse` would have applied
  (`applyVarAttr`/`applyFTask`/`applyCoverageBlock`/`applyCase`) and checks the
  post-width contract under `--debug-check`.
- `process()` skips `V3LinkParse`…`V3WidthCommit` in VHDL mode and commits the width
  invariants directly. `.vlt` control files stay on the SV parser; the standard package,
  libmaps and hierarchical parameter files are skipped.
- Mixed SV/VHDL sources are rejected with an error.

Verified at WP0: an entity with an empty architecture produces a C++ model that compiles
and runs (`--binary`); port types (`std_logic`, `std_logic_vector`, `unsigned`, `signed`,
`integer`, `boolean`) map to the expected dtypes; an entity without an architecture is
rejected; `DECLFILENAME` is suppressed by a `.vlt` waiver; `test_regress/t/t_EXAMPLE.py`
still passes.

Known WP0 limits (all intentional, all rejected with diagnostics): architecture
declarative/statement parts, packages, generics, components, configurations, expressions
and all sequential/concurrent statements. VHDL library lookup through `-y`/`-v` is
deferred because `V3LinkCells::autoLoad` still uses the SV parser. Build note: `make`
stops at the `verilator_gantt.1` target when `help2man` is absent; both compiler binaries
build.

Next: WP1 replaces the stub with the real VHDL subset parser (architecture declarative and
statement parts, subtype declarations, expressions, subprograms), then WP2 semantics.

## 1. Approach

The frontend emits an AST in the shape produced by `V3WidthCommit` and re-enters
`process()` after the width stage; from there the existing pipeline is reused unchanged.
Parsing and semantic analysis are replaceable: the active path delegates them to GHDL
(`ghdl --file-to-xml`) and keeps only the XML reader and lowering in Verilator, while the
reserved path performs them in-tree. Both produce the same committed AST, so elaboration
and lowering are shared work rather than duplicated front ends.

Rationale:

- VHDL elaboration (visibility, overload resolution, generic binding, array subtype
  derivation, entity/architecture binding, resolution functions) has no counterpart in
  `V3LinkDot`/`V3Param`/`V3Width`. Reusing those passes would require pretending VHDL is
  SystemVerilog, and their semantics would then fight the frontend.
- The post-width AST is small and stable. A counter is one `AstModule`, three `AstVar`s,
  one `AstAlways`, two `AstAssignDly`; that is the whole contract.
- `V3Width::width()` clears `didWidth` and re-coerces every expression using SV rules, so
  it cannot be used as a consistency pass on a VHDL-typed AST.

## 2. Scope

### 2.1 Target profile, with the M1 demo split

The table is the full synthesizable target (the "solid" profile). The `Demo` column marks
the narrower M1 profile: one clock domain plus combinational logic, no user-defined
overloads, no records/arrays, no configurations, and no user packages (only a minimal
`numeric_std` slice). Everything marked `no` is M1.1 or later. The demo exists so the
end-to-end pipeline can be validated before the expensive parts of the semantic analysis
are written.

| Area | Target support | Demo |
|---|---|---|
| Design units | `entity` + one `architecture` per entity | yes |
| Design units | `package` and `package body` (constants, types, subprograms) | no |
| Ports | `in`, `out`, `inout`, `buffer` (mapped to `out`); named, positional and `open` maps | yes |
| Types | `std_logic`, `std_ulogic`, `std_logic_vector`, `unsigned`, `signed`, `integer`/`natural`/`positive` with static ranges, `boolean`, enumerations | yes (arithmetic via the minimal `numeric_std` slice below) |
| Types | records, constrained arrays, subtypes, aliases | no |
| Objects | signals, variables, constants | yes |
| Objects | deferred constants in package bodies | no |
| Concurrent statements | simple, conditional (`when`/`else`) and selected (`with`/`select`) signal assignment | yes |
| Processes | sensitivity list, `all`; sequential `if`/`case`/`for` (static bounds) | yes |
| Processes | `case?`, `next`/`exit` | no |
| Subprograms | functions and procedures, `in`/`out`/`inout` parameters, calls | no |
| Instantiation | direct entity instantiation | yes |
| Instantiation | `component` declarations with default binding | no |
| Generics | constant generics with static actuals and defaults | yes |
| Attributes | `'event`, `'stable`, `'left`, `'right`, `'high`, `'low`, `'length`, `'ascending`, `'range` | yes |
| Attributes | `'image`/`'value` | no |
| Packages | full `std_logic_1164` and `numeric_std` intrinsic subset | partial: minimal `numeric_std` slice (`+`, `-`, comparisons, `to_unsigned`/`to_signed`, `resize`) |
| Values | 9-state `std_logic` collapsed: `U`/`W`/`-` → X, `L` → 0, `H` → 1 | yes |
| Drivers | one driver per signal; `inout` via tristate | yes |
| Source language | **one language per invocation**; mixed SV/VHDL inputs are rejected in M0/M1 (see §3.5) | yes |

### 2.2 Later milestones (M2+)

Resolution functions (including multi-driver `std_logic`/`std_logic_vector`), user-defined
resolution; unconstrained array ports specialized per instance; component configurations
and per-instance architecture selection; `wait on`/`wait until`/`wait for` (requires
`--timing`), `after`, inertial/transport delay; access/protected/file/physical types and
`textio`; PSL; type/subprogram generics and generic packages; VHDL verification libraries
(OSVVM, VUnit).

### 2.3 Explicit non-goals

- Simulation-identical 9-state semantics, inertial delay and full delta-cycle scheduling.
- VHDL testbench constructs (file I/O, protected types, access types) in M1–M2.
- Changing Verilator's 2-state value model (`V3Number` encodes only 0/1/X/Z, and the
  runtime is 2-state).

## 3. Architecture

### 3.1 Seam

Parsing is text-in, AST-out with no return value: `V3Global::readFiles()` calls
`V3Parse::parseFile()` per file and grammar actions append modules directly to
`v3Global.rootp()`; `V3LinkCells::link()` runs at the end of `readFiles()`. The new
frontend plugs in at that boundary:

1. `V3Global::readFiles()` selects the frontend per file from the extension
   (`.vhd`, `.vhdl`) or an explicit `--language vhdl`.
1. **Per-file `parseFile()` parses only.** VHDL design units are order-independent
   (architectures, packages, entities and configurations may be in later files), so no
   Verilator AST may be emitted while files are still being read. The frontend appends
   parsed design units to its own design library.
1. **New `finish()` hook, called from `readFiles()` after the last VHDL file and before
   `V3LinkCells::link()`.** It performs semantic analysis over the whole design library,
   elaborates (generic binding, architecture/component selection, generate unrolling,
   array subtype specialization), and only then emits committed `AstModule`s into
   `v3Global.rootp()`. In SV mode there is no such hook; `readFiles()` calls `finish()`
   only on the VHDL frontend.
1. `V3LinkCells` runs unchanged (it is name/library based and cannot tell the source
   language). It assigns `level()`/`depth()` and normalizes pins. In VHDL mode it must
   not need to read additional files: `finish()` emits a module for every entity in the
   design library, using each entity's default architecture. `V3LinkCells` then computes
   levels and the top, and `V3Dead` removes modules that are not used.
1. A new VHDL binder (`V3VhdlLink`) runs after `V3LinkCells` and before `process()`:
   sets `AstPin::modVarp`, port `pinNum`s, `VAccess`, and asserts the AST contract
   (Section 4).
1. `process()` takes a source-language branch that skips the SV front-half passes and
   continues at `wrapTop`.

The VHDL frontend therefore has two entry points; no virtual interface is needed, because
`readFiles()` selects one language per run:

```cpp
class V3VhdlParse final {
public:
    // Per file: lex/parse only; append design units to the frontend's library.
    void parseFile(FileLine*, const string& filename, bool inLibrary, bool inLibMap,
                   const string& libname, const string& errmsg,
                   const std::string& notFoundName = "");
    // After all files: analyze, elaborate, emit committed AstModules.
    void finish();
};
```

`readFiles()` also parses inputs that are not user design units; VHDL mode handles them
separately and does **not** use one frontend object per run:

- `.vlt` control files (the standard waiver and user files) still go through the
  SystemVerilog parser. Parsing **registers** directives in `V3Control`; it does not apply
  all of them. Application is split across stages:
  - `V3Control::applyModule` runs in `V3LinkCells` (reused), so module-level directives
    such as `tracing_off` apply.
  - `V3Control::applyFTask`, `applyVarAttr`, `applyCoverageBlock` and `applyCase` run in
    `V3LinkParse` (skipped). VHDL mode must call these itself from `V3VhdlLink` after
    lowering, over the emitted `AstFunc`/`AstTask`, `AstVar`, `AstBegin`/`AstGenBlock`
    and `AstCase` nodes, using the same name resolution.
  - Warning ignores are applied through `FileLine::applyIgnores`; the frontend must create
    token `FileLine`s through `copyOrSameFileLineApplied()` (or call `applyIgnores()`), so
    `.vlt` warning suppressions and `lint_off`/`lint_on` behave as in SV runs.
    Directives that cannot be mapped (for example, constructs the M1 subset does not
    support) must produce an explicit unsupported-directive warning rather than being
    silently dropped. Add tests that each supported directive takes effect on a VHDL
    design.
- `verilated_std.sv` is skipped: the VHDL frontend emits no `std::` references, so
  `AstNetlist::stdPackagep()` remains null.
- Libmap files and hierarchical-parameter files are skipped; VHDL library selection is
  handled by the frontend (`--work`, `-y`, `-v`).
- Only user VHDL design units are collected for the VHDL frontend's `finish()`.

The control/SV parser and the VHDL frontend are two separate instances in `readFiles()`;
`finish()` is invoked only on the VHDL one.

### 3.2 Pipeline gate

| Pipeline stage | SV | VHDL |
|---|---|---|
| `V3PreShell`/`V3PreProc` (Verilog preprocessor) | yes | skip for VHDL sources (still used for `.vlt` control files) |
| `V3ParseImp` + lexer + bison grammar | yes | replaced by VHDL frontend |
| `V3LinkCells::link` | yes | reuse |
| `V3LinkLevel::modSortByLevel` | yes | reuse (needs `level()` from `V3LinkCells`) |
| `V3LinkParse::linkParse` | yes | skip |
| `V3LinkDot::linkDotPrimary` | yes | skip |
| `V3LinkResolve`, `V3LinkLValue`, `V3LinkJump`, `V3LinkInc` | yes | skip; frontend emits lowered forms |
| `V3Param::param` (generics/parameters, generate unrolling) | yes | skip; frontend elaborates |
| `V3LinkWith` | yes | skip |
| `V3Dead::deadifyModules` | yes | reuse (removes unreachable modules) |
| `V3Udp::udpResolve` | yes | skip (no UDPs) |
| `V3Width::width`, `V3WidthCommit::widthCommit` | yes | skip; frontend commits widths |
| `V3LinkLevel::wrapTop` | yes | reuse |
| `V3Undriven` … `V3EmitC`, runtime | yes | reuse |

The gate must be explicit and documented in `Verilator.cpp`. `V3LinkLValue` may be reused
instead of setting `VAccess` in the binder, but only if its assumptions hold (resolved
`AstNodeVarRef`s and `AstPin::modVarp`); the binder owns this decision.

### 3.3 New components

| File(s) | Responsibility |
|---|---|
| `src/V3VhdlParse.{h,cpp}` | driver: file dispatch, parse-tree construction, diagnostics |
| `src/V3VhdlLexer.{h,cpp}` | hand-written lexer (context-sensitive `'` for attributes vs character literals, based/bit-string literals) |
| `src/V3VhdlAst.h` | frontend-internal parse/AST types (not Verilator AST nodes) |
| `src/V3VhdlSem*.{h,cpp}` | symbol tables, visibility, overload resolution, types, static evaluation |
| `src/V3VhdlElab.{h,cpp}` | entity/architecture/component binding, generics, generate unrolling, array subtype specialization |
| `src/V3VhdlLower.{h,cpp}` | VHDL AST → Verilator AST |
| `src/V3VhdlLink.{h,cpp}` | pin/port binding, `VAccess`, control-directive application (`applyFTask`/`applyVarAttr`/`applyCoverageBlock`/`applyCase`, `FileLine::applyIgnores`), AST-contract assertion |
| `src/V3VhdlStdPkg.{h,cpp}` | `std_logic_1164` and `numeric_std` intrinsics |

The frontend is single-threaded (`VL_MT_DISABLED`), like the existing parser. No new
Verilator AST nodes are planned for M1; if one becomes unavoidable, it is added with
`astgen`, and every pass that must understand it is updated in the same change.

### 3.4 Build integration

- Add the new objects to `RAW_OBJS` in `src/Makefile_obj.in` (the header list is a
  wildcard: `HEADERS = $(wildcard V*.h v*.h)`).
- Add the new sources to the source list in `src/CMakeLists.txt` and headers to its
  `set(HEADERS ...)` list.
- No bison/flex generation is required for a hand-written lexer/parser. If flex is used,
  mirror the `verilog.l` rule (including the `flexfix` post-processing step) for the new
  lexer.
- No AST generator change is required for M1.

### 3.5 Mixed-language designs (M2)

The M1 pipeline gate is run-global: if any VHDL **design** source is present, the whole
invocation uses the VHDL frontend and `process()` runs the VHDL path (control `.vlt` files
are still parsed by the SV parser, as described in §3.1). It cannot process SV modules
through `V3Width` and pre-widthed VHDL modules in the same tree, because those passes walk
the entire netlist. M0/M1 therefore reject mixed inputs with an explicit error, including
VHDL files reached through `-v`/`-y` libraries.

M2 options, in order of preference:

1. **Library block boundary.** Run the VHDL frontend on one VHDL hierarchy with a single
   top entity and `--lib-create <name>`. That flow already emits C++, a Verilog wrapper
   and a Makefile that builds the DPI library
   (`docs/guide/exe_verilator.rst:1367-1385`). The SV top is then a pure-SV invocation
   that instantiates the generated wrapper and links the library. No netlist is shared;
   the ABI is the generated wrapper. The first M2 task is a proof of concept that a
   VHDL-only AST flows through `--lib-create` unchanged and that the generated wrapper
   can be instantiated from an SV run. No new output format is required.
1. **Parse-level emission for VHDL leaf modules.** Emit VHDL modules in SV-parse shape
   (`AstRefDType` by name, explicit casts/resizes) and let the SV pipeline elaborate them.
   This reintroduces dependence on `V3Width` semantics and is only viable for leaf modules
   with simple ports; it must not be used for VHDL types that SV cannot express.
1. **Per-module bypass.** Mark pre-elaborated modules and teach every front-half pass to
   skip them. Invasive (touches `V3LinkDot`, `V3Param`, `V3Width` and their symbol tables),
   high regression risk, not recommended.

Mixed-language support is not implied by the M1 design; option 1 is the M2 target and
requires a separately reviewed change.

### 3.5.1 Cross-language binding contract (UG901-compatible)

The contract follows Vivado UG901 "Instantiating Verilog in VHDL" so that designs which
synthesize there behave the same here. GHDL 7.0.0-dev behavior was measured to confirm the
analysis side (2026-11-08).

- **SV into VHDL needs no contract.** Once VHDL entities are emitted as `AstModule`s, an
  SV `AstCell` binds to them by module name in `V3LinkCells`, exactly as for SV-to-SV.
- **VHDL into SV requires a component declaration.** Declare a VHDL component with the same
  name as the SV module and instantiate it as a component, as UG901 specifies. Direct
  entity instantiation of a Verilog module is not supported there, and Verilator must not
  accept it through generated stubs - that would compile designs Vivado rejects. GHDL
  agrees at analysis: `entity work.svmod` reports
  `unit "svmod" not found in library "work"` and aborts `--file-to-xml`, while a declared,
  unbound component analyzes cleanly and defers binding. Because GHDL checks only the port
  map against the component declaration, the frontend validates the component's ports
  (direction and width) against the bound entity before emitting the cell.
- **Binding algorithm (UG901).** Treat the component name as a VHDL design-unit name and
  search `work`; if a VHDL unit is found, bind it; otherwise treat the name as a Verilog
  module name and search **case-sensitively**; bind the match. Configuration declarations
  and non-default binding are out of scope, as they are there. Only Verilog modules are
  visible across the boundary.
- **Unified library.** An SV module and a VHDL entity with the same name cannot coexist in
  one logical library, so that is an error, not a silent preference. Verilator already
  reports duplicate declarations (`MODDUP`, `src/V3LinkCells.cpp:875`) and warnings are
  errors by default, so UG901's "first match" rule has no ambiguous equivalent.
- **Case handling.** VHDL identifiers are case-insensitive for lookup, but the Verilog
  search must observe case. The frontend must therefore keep the original spelling of
  component and port identifiers. The WP0 lexer currently folds identifiers to lowercase in
  `tok.text` (`src/V3VhdlParse.cpp:250-254`); WP1 must keep the original text and add a
  folded form for scope lookup and keyword comparison, so `AstVar`/pin names carry the
  spelling the SV side must match.
- **Elaboration stays in Verilator.** `ghdl --file-to-xml` is pre-elaboration, and
  `ghdl synth` reports `instance "u1" of component "svmod" is not bound [-Wbinding]` and
  drops the instance. Any GHDL-derived IR stops at the analyzed, type-resolved design unit;
  component binding, generics/generates and hierarchy construction are Verilator's WP3.

Port name/type/direction/width compatibility is defined by the companion UG901 page (not
quoted here). Treat the specific type mappings as **unverified** until checked against the
target tool; enforce whatever is confirmed as a debug-checked contract in `V3VhdlLink`.

## 4. AST contract (normative)

The emitted tree must satisfy the following before it enters `process()`.

| # | Requirement | Consumer / enforcement |
|---|---|---|
| 1 | Modules are `AstNodeModule` children of `AstNetlist::modulesp()`; every node has a valid `FileLine` | `V3LinkCells`, diagnostics |
| 2 | No `AstParseRef`; every `AstNodeVarRef` has non-null `varp()` and a valid `VAccess` | `V3LinkLValue` (if reused), `V3Broken`, `V3Const` |
| 3 | No `AstPort`; module ports are `AstVar` with `varType(PORT)`, `direction`, and `pinNum` in declaration order | positional pin binding |
| 4 | Every `AstVar` has `dtypep()`; every expression has `dtypep()`; no `childDTypep()` remains; `widthMin() == width()` | `V3Broken`, `assertDTypesResolved`, `V3Const` |
| 5 | Dtypes are canonicalized through `AstTypeTable` (`findBasicDType`, `findLogicBitDType`) so equal dtypes are equal pointers | dtype equality checks downstream |
| 6 | `AstConst` carries a `V3Number`; enum items have explicit `valuep()` values (V3Width normally fills them) | `V3Const`, `V3Dead`, enum lowering |
| 7 | No `AstGenIf`/`AstGenFor`/`AstGenCase`; generates are unrolled during elaboration | `V3Param` is skipped |
| 8 | Each `AstCell` has non-empty `origName()` and a `modName()` resolvable to a module; each `AstPin` has `modVarp()` bound, a name or 1-based `pinNum`, and `exprp == nullptr` for `open` | `V3Inst`, `V3Dead`, `V3VhdlLink` |
| 9 | Concurrent assignment is `AstAlways{ AstAssignW }` (`CONT_ASSIGN`); process signal assignment is `AstAssignDly`; variable assignment is `AstAssign` | `V3Active`, `V3Delayed` |
| 10 | Processes carry an `AstSenTree`: `EDGE_POS`/`EDGE_NEG` for recognized clock idioms, `ET_CHANGED` per listed signal otherwise; `process(all)` emits `ALWAYS_COMB` with a null sentree | `V3Active`, `V3Sched` |
| 11 | One NBA target per `AstAssignDly` | `V3Delayed` |
| 12 | No `AstActive`/`AstScope`/`AstVarScope`/`AstCFunc` | created by later passes |

Validate the contract with a debug-only checker in `V3VhdlLink` rather than relying on
downstream assertions.

## 5. Work packages

| WP | Deliverable | Exit criterion | Est. |
|---|---|---|---|
| WP0 | **Done.** Plumbing: `V3SourceLang` detection, `.vhd`/`.vhdl` sourcing, frontend entry points (`parseFile`/`finish`), context clauses, entity ports, empty architecture binding, control-input handling (`.vlt` parsed and applied, std package/libmap/hier-params skipped), pipeline gate, mixed-input rejection, empty module emission | `verilator --language vhdl` on an entity with an empty architecture produces and runs a C++ model; module, function, variable and warning-suppression directives from `.vlt` take effect on a VHDL design; an entity without an architecture is rejected; a mixed SV/VHDL run fails with a clear error; `make test` unaffected | 2–4 wk |
| WP1 | In-tree lexer + parser + frontend AST (reserved alternative; the active path is GHDL XML). Full VHDL-2008 design-unit grammar for the M1 subset; diagnostics with VHDL `file:line` | Parses the M1 corpus; syntax errors match GHDL's line numbers | 3–6 PM |
| WP2 | Semantics: declarative regions, visibility/`use`, overload resolution, types/subtypes, static expression evaluation with VHDL range rules | Accepts/rejects semantic corpus identically to GHDL for M1 constructs | 5–10 PM |
| WP3 | Elaboration: entity/architecture/component binding, constant generics, generate unrolling, array range specialization, deferred constants | Hierarchy and instance names match GHDL's elaboration | 3–6 PM |
| WP4 | Lowering to Verilator AST including the Section 4 contract, clock-idiosyncrasy recognition, port/pin binding, attribute lowering | Counter, FIFO, FSM, UART, AXI-Lite pass simulation comparison | 2–4 PM |
| WP5 | `std_logic_1164` + `numeric_std` intrinsics | Numeric test corpus matches GHDL for 0/1 (X/Z where defined) | 2–4 PM |
| WP6 | Test infrastructure: VHDL sources in `test_regress`, differential harness against GHDL, AST-dump golden tests | CI runs VHDL tests; contract checker enabled under `--debug-check` | 2–4 PM |
| WP7 | Hardening: diagnostics, unsupported-feature errors, performance, control-file interaction, cross-run determinism | No silent miscompiles; documented loss list enforced by tests | 2–4 PM |
| WP8 (M2) | Mixed-language support per §3.5 (separately reviewed); first task is the `--lib-create` proof of concept | VHDL hierarchy compiled with `--lib-create`; its generated wrapper is instantiated from an SV run | 2–6 PM |

WP1–WP7 overlap, and their estimates are for the solid profile. The solid profile totals
16–33 person-months; WP8 is M2 and excluded. The M1 demo profile (§2.1 `Demo` column) is
WP0 plus reduced WP1–WP4, a minimal WP5 slice, and minimal WP6: 5–10 person-months. M1.1
completes WP1–WP4, adds the full WP5 and WP7, and reaches the 16–33 person-month total.

## 6. VHDL → Verilator AST mapping

| VHDL | Emit | Notes |
|---|---|---|
| `entity` + `architecture` | one `AstModule` | name = entity; ports as `AstVar`; multiple architectures need mangling (M2) |
| `package`, `package body` | `AstPackage` | body merged into the package after two-phase constant binding |
| `std_logic`, `std_ulogic`, `boolean` | 1-bit `AstBasicDType(LOGIC)` | 9-state collapse at elaboration; warn on `U`/`W`/`-`/`L`/`H` literals |
| `std_logic_vector(N downto 0)` | `AstBasicDType(LOGIC)` with range, or packed array | use `findLogicBitDType`; keep declared direction |
| `unsigned`/`signed` | same with `VSigning::UNSIGNED`/`SIGNED` | operators lowered explicitly, never SV implicit rules |
| `integer`, `natural`, `positive` subtypes | 32-bit signed `INT` | static range checks in the frontend; runtime checks unsupported (documented loss) |
| enumeration | `AstEnumDType` with explicit `AstEnumItem::valuep` | literals as typed `AstConst` |
| record | `AstStructDType` + `AstMemberDType` | field access via `AstStructSel` |
| constrained array | `AstUnpackArrayDType` | normalize index expressions to 0-based; map `'range` to constants |
| subtype, alias | `AstTypedef` + resolved `AstRefDType` | resolve to a concrete dtype before width-commit shape |
| constants, deferred constants | `AstVar` with `valuep` + explicit `AstConst` folds | no `V3Param`/`V3Const` dependency for elaboration values |
| concurrent assignment | `AstAlways{ AstAssignW }` | conditional → `AstCond`, selected → `AstCase`/`AstCond` chain |
| process signal `<=` | `AstAssignDly` | keep NBA semantics; one target per assignment |
| variable `:=` | `AstAssign` | immediate update |
| process sensitivity list | `AstSenTree`/`AstSenItem` | clock idioms → `EDGE_POS`/`EDGE_NEG`; else `ET_CHANGED` |
| `process(all)` | `AstAlways` + `ALWAYS_COMB`, null sentree | |
| `if`, `case`, `case?` | `AstIf`, `AstCase` (`casez()` for `case?`) | |
| static `for ... loop` | unrolled statements/instances | generate unrolling is frontend work |
| dynamic `for`/`while` | `AstLoop`/`AstLoopTest` + `AstJumpBlock` | M2; must not rely on `V3LinkJump` |
| `next`, `exit` | `AstJumpGo`/`AstJumpBlock` | emit lowered form directly |
| function/procedure | `AstFunc`/`AstTask` with bound calls (`AstFuncRef`/`AstTaskRef`) | parameters as `AstVar`; result dtype explicit |
| direct instantiation | `AstCell` + `AstPin` | binder sets `modVarp`; positional pins get `pinNum` |
| component instantiation | same after default binding | M2 configurations |
| constant generic | substituted during elaboration | modules are emitted per generic set, like `V3Param` clones |
| concurrent/selected assertion | `AstAssert`/`AstDisplay` lowering | M2 for full severity semantics |
| `'event`, `'stable` | edge sen items / `AstStable`/`AstPast` | recognize `rising_edge`/`falling_edge` first |
| `'left`/`'right`/`'high`/`'low`/`'length`/`'ascending` | constant folds or `AstAttrOf` | prefer constant folds |
| `'delayed`, `after`, `wait for` | `AstDelay`/`AstWait` | M2, requires `--timing`; inertial delay unsupported |
| multi-driver resolved signals | explicit resolution logic | M2; no user resolution in M1 |
| access/protected/file/physical/textio | error | out of scope with a clear diagnostic |

## 7. Semantic decisions and documented losses

| Decision | M1 | Consequence |
|---|---|---|
| 9-state values | collapse to 0/1/X/Z | `U`, `W`, `-` become X; `L`/`H` become 0/1 |
| integer range | static checks only | runtime range violations are not detected; documented synthesis-oriented loss |
| delta cycles | VHDL signal assignment → NBA; Verilator NBA/settle scheduling | ordering across processes is not VHDL-identical; validate with differential tests |
| process classification | explicit sensitivity list + NBA keeps a level-sensitive active | slower than VHDL-aware combinational inference; optimize later |
| multiple architectures | last analyzed architecture is the default; explicit selection is an error | no configuration support in M1; M2 adds selection |
| multiple drivers | one driver per signal in M1 | M2 inserts per-bit resolution logic for `std_logic(_vector)` |
| generics | fully elaborated in the frontend (VHDL-only runs) | no `AstParam`/`V3Param`; cross-language generic/parameter passing is **unverified**: UG901's instantiation rules do not cover it, so `AstParam` on VHDL modules vs. monomorphized variants stays TBD until checked against the target tool |
| names | VHDL identifiers keep their original spelling for emitted `AstVar`/pin/instance names; a folded form keys scope lookup; both encoded with `AstNode::encodeName` | cross-language binding is case-sensitive per UG901; VCD/debug show source spelling |
| timing | no `after`/`wait` in M1 | M2 behind `--timing`; inertial/pulse rejection never |

## 8. CLI and user experience

- File extensions `.vhd`, `.vhdl`; add them to library search extensions.
- `--language vhdl` for extensionless files; do not extend `V3LangCode` (it selects
  Verilog keyword standards and drives the `` `begin_keywords `` directive). Add a
  separate source-language selector.
- `--top-module <entity>` selects the top entity; error with spell-check if missing.
- `--work <library>` and `-y`/`-v` apply to VHDL design units.
- `ieee` is a built-in library; `work` is the default.
- Diagnostics use the existing `FileLine` mechanism so errors point at VHDL source
  locations. Add new error codes only when a distinct message needs one, and update
  `docs/guide/warnings.rst` plus tests in the same change.
- Tracing uses VHDL signal names; generate labels become hierarchy components.

## 9. Testing

| Layer | What | How |
|---|---|---|
| Unit | lexer/parser and frontend AST | parse-only debug mode with AST dump; golden parse files in `test_regress` |
| Contract | Section 4 invariants | `V3VhdlLink` debug checker; run all VHDL tests with `--debug-check` |
| AST | lowering shape | `--dump-tree` golden files after width-commit-shaped emission |
| Simulation | functional equivalence | random-vector differential runs against GHDL |
| Regression | M1 design corpus | counter, shift register, FIFO, FSM, UART TX/RX, AXI-Lite slave, memory wrappers |
| Negative | unsupported constructs | exact diagnostics, no silent acceptance |
| Mixed language | M2 only: VHDL hierarchy instantiated from SV through the §3.5 `--lib-create` wrapper/library flow; mixed sources in one invocation are rejected in M1 | separate M2 test suite |
| Control files | `.vlt` waiver and control directives still apply in VHDL runs (libmap/hier-params are skipped) | run the waiver corpus with VHDL sources; assert the expected waivers fire |

Differential harness: compile the VHDL sources with GHDL, drive both models with the same
random stimulus (C++/DPI harness for Verilator, generated VHDL testbench for GHDL), compare
outputs cycle by cycle. `test_regress` already knows how to invoke GHDL as a reference
simulator, so reuse its configuration and tool discovery, but add a VHDL-source mode to
`test_regress/driver.py` (the current harness builds Verilog/SV sources only).

CI: new tests must not require GHDL for the default suite; gate differential tests on
`VERILATOR_GHDL` being set, as the existing cross-simulator tests do.

## 10. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| VHDL semantics replicated incorrectly (overloads, ranges, aggregates) | silent miscompiles | semantic corpus compared against GHDL; fail closed on ambiguity |
| Delta-cycle ordering approximation | subtle simulation mismatch | differential tests with multi-process designs; document the restriction |
| Level-sensitive processes with NBAs are slow | performance regression vs hand-written SV | recognize clock idioms; later combinational inference and process splitting |
| Skipping `V3Width` means the frontend owns constant folding | frontend must fold elaboration-time expressions | implement VHDL static evaluation once and use it for ranges, generics, attributes, case choices |
| Contract drift | downstream assertion failures | debug checker in `V3VhdlLink`; golden AST dumps |
| Frontend maintenance cost | second language frontend forever | keep the subset explicit; no new AST nodes; reuse every generic pass possible |
| Licensing | cannot link GHDL (GPL-2.0) or nvc (GPL-3.0) into `verilator_bin` under LGPL/Artistic | write the frontend from scratch, or consume an out-of-process helper's IR; keep external tools out-of-process |

## 11. Milestones

| Milestone | Content | Duration |
|---|---|---|
| M0 | WP0 plumbing + empty entity end-to-end (**done**) | 2–4 weeks |
| M1 | Demo profile (§2.1 `Demo` column): reduced WP1–WP4 end-to-end, minimal `numeric_std` slice, minimal WP6, demo corpus | 5–10 person-months |
| M1.1 | Solid profile: remaining WP1–WP4 work + full WP5 standard packages + full WP6/WP7 | +11–23 person-months (cumulative 16–33) |
| M2 | resolution logic, unconstrained ports, configurations, waits/timing, mixed-language WP8 | +6–12 person-months |
| M3 | type/subprogram generics, asserts/PSL, performance, ecosystem | beyond |

## 12. Open decisions

1. Hand-written lexer/parser (recommended; VHDL's `'` and aggregate/name ambiguities make
   LR(1) painful) versus bison + lexer hacks.
1. Reuse `V3LinkLValue` or set `VAccess` in `V3VhdlLink`.
1. `integer` representation: 32-bit `INT` with static checks (recommended) versus
   narrowed synthesis widths per subtype.
1. Clocked-process recognition heuristics: `rising_edge`/`falling_edge`/`'event and clk = '1'` only (recommended) versus requiring user attributes.
1. M2 mixed-language route: `--lib-create` wrapper plus linked library (recommended), parse-level
   emission for VHDL leaf modules, or per-module bypass (§3.5). M1 rejects mixed inputs.
1. Frontend source: hand-written frontend (current recommendation) versus an out-of-process
   GPL helper that emits the analyzed, type-resolved GHDL IIR for Verilator to elaborate and
   lower. `ghdl --file-to-xml` carries `kind`, `file`/`line`/`col` on every node and resolved
   references (`implementation`, `named_entity`, `type`), so it already contains the WP2
   result, but no elaboration (§3.5.1). The helper removes WP1-WP2 and most of WP5, keeps
   WP3-WP7, and confines GHDL version coupling to the helper; its cost is an IR reader and a
   pinned producer.
1. Whether multi-driver `std_logic` resolution is in M1 (recommended: no) or M2.

## 13. Evidence references

All line numbers in this checkout (`9c28d84`).

- Pipeline: `src/Verilator.cpp:150-243` (link/param/width), `:244-460` (post-width),
  `:754-756` (`readFiles` call), `:902` (preprocessor boot).
- File dispatch: `src/V3Global.cpp:82-160`; `src/V3ParseImp.cpp:371-459`, `:493-505`;
  `src/V3PreShell.cpp:104-119`; `src/V3Options.cpp:717-727`, `:2399-2401`.
- Language type: `src/V3LangCode.h:33-58` (SV standards only).
- SV typing rules (why `V3Width` is not reusable): `src/V3Width.cpp:17-70`, `:11875-11898`.
- Generate/parameter elaboration: `src/Verilator.cpp:182-185`;
  `src/V3Param.cpp:2905`, `:3817`, `:3879`; hard requirement that `AstGenFor` is wrapped in
  `AstGenBlock` at `src/V3Param.cpp:3879-3881`.
- Pin binding: `src/V3LinkCells.cpp:274-347`, `:700-763`; `AstPin::modVarp` consumers
  `src/V3Inst.cpp:56`, `:129-156`; `src/V3Dead.cpp:372`; `src/V3LinkLValue.cpp:101-110`.
- Process classification and NBA handling: `src/V3Active.cpp:463-499`, `:617-621`;
  `src/V3Delayed.cpp:15-63`; scheduling/settle: `docs/internals.rst:290-430`.
- AST nodes: `src/V3AstNodeOther.h:1582` (`AstNetlist`), `:2314` (`AstVar`), `:3425`
  (`AstModule`), `:3487` (`AstAlways`), `:1746` (`AstPin`), `:1788` (`AstPort`);
  `src/V3AstNodeStmt.h:1533` (`AstWait`), `:1617` (`AstAssignDly`);
  `src/V3AstNodeDType.h:452` (`AstBasicDType`), `:957` (`AstEnumDType`),
  `:1590`/`:1599` (packed/unpacked arrays), `:1643` (`AstStructDType`).
- Value model: `src/V3Number.h:84-89` (0/1/X/Z encoding); `docs/guide/languages.rst:249`
  (2-state).
- Invariants: `src/V3Ast.cpp:1289` (`checkTreeIter`); `src/V3Broken.cpp:187-217`;
  `src/Verilator.cpp:222-225`.
- AST generation: `docs/internals.rst:1330-1420` (`astgen`).
- Build wiring: `src/Makefile_obj.in:158` (header wildcard), `:192-205` (objects);
  `src/CMakeLists.txt:41` (headers), `:345` (sources).
- Test infrastructure: `test_regress/driver.py` (GHDL as reference tool);
  `docs/internals.rst:2565`, `:2620` (`--ghdl`, `VERILATOR_GHDL`).
- Front-half size reference: `verilog.y` 8809 lines; generated parser 677 tokens,
  739 nonterminals, 3720 rules, 7612 states (`src/obj_opt/V3ParseBison.c:2001-2007`);
  503 AST node types (`src/obj_opt/V3Ast__gen_type_enum.h:507`).
- VHDL reference implementations: GHDL `src/vhdl` ≈125k LOC (parse 14.2k, semantics
  35.1k, nodes 33.8k); nvc ≈78.5k LOC (parse 13.8k, semantics/elaboration ≈15k,
  lowering 13.2k).
