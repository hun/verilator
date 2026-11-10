<!-- SPDX-FileCopyrightText: 2026 Hannes Klas
     SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0 -->

# VHDL RTL-to-RTL translator -- implementation plan

**Status:** proposed; not started. This plan is deliberately gated on the VHDL frontend for
Verilator (`VHDL_FRONTEND_PLAN.md`) being integrated and stable. It is not user
documentation and must not be linked from `docs/`.

**Target tree:** Verilator 5.055 devel, branch `vhdl-frontend-wp0`, commit `8a39064`
(2026-11-09). The tree moves; treat line numbers as orientation, not as a contract.

**One-line verdict:** the approach is a good base for an RTL translator, but as a *sibling
backend*. Reuse the GHDL-XML front half and the semantic/elaboration work; do not try to
derive RTL output from the existing Verilator lowering or from `V3EmitV`.

## 0. Prerequisite and decision gate

Do not start WP-T2 or later until the VHDL frontend is integrated and stable enough that
the simulation backend's behaviour is a trustworthy oracle. WP-T0 is the first deliverable
and exists because the frontend source is still an open question.

### WP-T0: frontend decision gate (after integration)

Decide, with measured evidence:

| Question | Keep GHDL XML | Replace with in-tree frontend |
|---|---|---|
| Analysis/elaboration coverage and diagnostics | Adequate for the translator's target subset | Needed for constructs GHDL XML does not expose |
| Dependency on an out-of-process GPL tool | Acceptable for the product | Not acceptable |
| Pinned GHDL version and XML drift | Tolerable, with a version check and liberal parsing | Unacceptable for a long-lived product |
| Elaborated-design information | Translator does its own elaboration (already the plan) | Frontend can provide it directly |
| Build/packaging and startup cost | Acceptable | Too costly |

Recommendation: prefer the **frontend-neutral design model** in Section 3 either way. The
translator must not care whether the model is built from GHDL XML or from an in-tree
parser. If the frontend is replaced first, only `VhdlDesignFromXml` is swapped for
`VhdlDesignFromAst`; WP-T2 onward is unchanged.

## 1. Product definition

### 1.1 What this is

A **synthesizable-subset, RTL-preserving VHDL-2008 to SystemVerilog translator**:

- entity/architecture -> module with named ports and parameters
- concurrent statements -> continuous assignments or combinational `always`
- processes -> `always_ff` / `always_comb` / `always @(...)`, one per VHDL process where
  the VHDL process is representable as one Verilog process
- subprograms -> SystemVerilog functions/tasks, with overload mangling, inlined only when
  SystemVerilog cannot express the signature
- records -> packed structs; constrained arrays -> packed or unpacked arrays
- generate -> generate-for/generate-if
- generics -> parameters where the target dialect can express them, otherwise
  specialized modules (decision, Section 5)
- packages -> SystemVerilog packages
- source names, hierarchy labels, comments and source locations preserved

Primary target: **SystemVerilog-2017**. A restricted Verilog-2001 mode is a later option
and is only useful for designs that stay within scalar/vector types (Section 5).

### 1.2 What this is not

- Not a netlist generator. `ghdl --synth --out=verilog` already produces a Verilog
  netlist, and `ghdl-yosys-plugin` produces RTLIL. This plan exists because those outputs
  do not preserve RTL structure.
- Not a simulator replacement. No delta-cycle fidelity, no full 9-state semantics, no
  testbench constructs.
- Not an extension of `V3EmitV`. That is a debug/lib-create emitter: it drops `AstCell`
  (`src/V3EmitV.cpp:1623`), emits no module port list (`:178`), and emits constructs such
  as `continuous assign` (`:350`). It is not a legal-Verilog writer.
- Not an extension of the current lowering. The lowering deliberately packs records and
  arrays, inlines subprograms, unrolls generates and splits processes; that is the wrong
  shape for RTL output (Section 2).

### 1.3 Why not reuse the current lowering

The existing path is a good *netlist* lowerer, not an RTL lowerer:

- Arrays are "modeled as one flat vector with `elemWidth` bits per element"
  (`src/V3VhdlParse.cpp:971`); records are packed vectors (`RecordInfo`, `:288`, `:563`).
- Functions and procedures are inlined (`inlineFunction`, `:2042`; procedure inlining,
  `:2262`).
- Generates are unrolled (`lowerForGenerate`, `:5370`).
- Processes are split and reordered (`emitProcess`, `:5285`).
- The emitted AST contract forbids `AstGen*` and requires fully resolved widths
  (`VHDL_FRONTEND_PLAN.md` Section 4, item 7).

Those choices are correct for Verilator's simulation contract. They are exactly the
information an RTL translator must preserve, so a new emitter has to work from the
analyzed/elaborated VHDL model, not from the post-width AST.

## 2. Why the current *approach* is still the right base

The front half already provides the expensive, hard part:

- `ghdl --file-to-xml` produces a decorated syntax tree with source locations and
  resolved references (`implementation`, `named_entity`, `type`). GHDL has therefore
  already done name resolution, overload resolution and type analysis
  (`VHDL_FRONTEND_PLAN.md` Section 12). This is precisely what a hand-written translator
  such as vhd2vl struggles with.
- `V3VhdlXml` provides a small DOM over that tree, and `nodist/ghdl_xml` dumps it for
  inspection.
- `VhdlXmlFrontend` already contains the semantic helpers an emitter needs: type probing,
  range length, record layout, static integer evaluation, string generics, generic
  bindings, instance binding, generate selection, port specialization.

The XML is a *syntax* tree, not an elaborated design: GHDL's documentation states that
there is "no XML dump of the elaborated design" and warns that the XML shape changes
between versions (`ghdl/doc/using/CommandReference.rst`, `--file-to-xml`). Elaboration is
therefore still ours; that is already true for the Verilator frontend and does not change
for the translator.

## 3. Architecture: one design model, two backends

The current `VhdlXmlFrontend` owns `AstNetlist`/`AstModule`, emits AST nodes while walking
the XML, and mixes analysis with emission. Add a backend-neutral boundary:

```text
VHDL
  -> ghdl --file-to-xml (or in-tree frontend)
  -> analyzed design (entities, architectures, packages, types, resolved refs)
  -> elaboration (entity/architecture binding, generics, generate, port specialization)
  -> VhdlDesign model
       -> existing lowering -> post-width Verilator AST -> C++ simulation
       -> new RTL emitter  -> SystemVerilog
```

### 3.1 Model contents

`VhdlDesign` is the single source of truth for both consumers:

- design units: entities, architectures, packages, package bodies, configurations
- ports and generics, with source spelling and file/line
- declarations: signals, constants, variables, aliases, types/subtypes
- types: scalar (logic/bit/integer/enum), arrays (dimensions, direction, element type),
  records (members, order), access/protected/file (marked unsupported)
- expressions and statements: enough structure for both lowering and emission; *not*
  Verilator AST nodes
- resolved references: call -> subprogram declaration, name -> declaration, type mark ->
  type declaration
- elaboration results: entity -> architecture, generic values (or symbolic parameter
  references in structural mode), generate selections/unrollings, specialized port
  widths and record layouts
- instance tree: cells, labels, generic/port maps, bound entity

Invariants:

1. Dependencies point one way: `model -> consumer`. The model never stores or returns
   `AstNode*`; consumers never inspect `V3VhdlXmlNode` directly.
2. XML node kinds appear only in `VhdlDesignFromXml`. An in-tree frontend implements the
   same model and nothing downstream changes.
3. The existing lowering and the new emitter consume the same elaboration results. Do not
   fork semantic decisions; a divergence is a bug in the model boundary.

### 3.2 Elaboration modes

The model supports two modes, chosen per run:

- **Specialized (MVP):** generics are substituted, generate constructs are expanded,
  each instance gets concrete widths and record layouts. This reuses the current
  elaboration semantics and is the fastest route to a working translator. Emitted modules
  are specialized, akin to Verilator's `V3Param` clones.
- **Structural (later):** generics are preserved as SystemVerilog parameters and generate
  constructs are emitted as generate constructs. Widths and conditions may then reference
  parameters, so ranges in the model must be expressions, not only `int`. This is the more
  RTL-faithful mode and the reason the model must not bake in `int` ranges.

Do not make structural mode a prerequisite for the first useful translator. Specialized
output is still RTL if processes, subprograms, records, arrays and hierarchy are preserved.

### 3.3 Extraction strategy

Do this incrementally; the simulation path must not change:

1. Introduce the model types and a read-only facade over the existing XML traversal.
2. Move the semantic helpers (`probeTypeSpec`, `dtypeOf`, `rangeLength`,
   `recordLayout`, `constIntValue`, generic/string/type bindings) behind the facade.
3. Build `VhdlDesign` for the current supported subset and compare the existing lowering's
   output against `--dump-tree` goldens/hashes for the `t_vhdl_*` suite.
4. Port the existing lowering to consume the model. Keep behaviour identical; this is a
   pure refactor and is independently reviewable.
5. Add the RTL emitter as the second consumer. It must not require changes to the
   lowering.

If step 3 shows that a full model is disproportionately risky, fall back to a **narrower
contract**: the model exposes only what the emitter needs (types, ranges, declarations,
resolved call targets, elaboration results), and the emitter still walks XML for statement
structure. Do not let this fallback leak XML node kinds into the emitter's API.

## 4. Target language and mapping

### 4.1 Dialect decision

Implement **SystemVerilog first**. Verilog-2001 cannot express records, array ports,
functions returning arrays, `parameter type`, or `parameter string`; a Verilog-2001-only
translator is forced into flattening aggregates, which recreates the netlist problem this
project is trying to solve. Add a restricted Verilog-2001 mode only if a concrete user
requires it, and document the resulting flattening.

### 4.2 Mapping table

| VHDL | SystemVerilog | Notes |
|---|---|---|
| `entity` + `architecture` | `module` | entity name preserved; architecture selection documented |
| `in`/`out`/`inout`/`buffer` port | `input`/`output`/`inout` | `buffer` -> `output`; reading an `out` port is VHDL-2008-legal and must be checked per case |
| `std_logic`, `std_ulogic` | `logic` | 4-state output; `U`/`W`/`-` -> `x` where a literal must be emitted |
| `bit`, `bit_vector` | `bit` or `logic` | choose one and be consistent; `bit` is 2-state |
| `std_logic_vector`, `unsigned`, `signed` | `logic [left:right]` | keep declared direction; apply `$unsigned`/`$signed` at operation sites, never rely on SV implicit sizing |
| integer/natural/positive subtype | `logic signed [high:low]` (width from subtype range) or `int` for plain `integer` | VHDL range is a value constraint; document overflow/truncation differences and verify |
| enumeration | `typedef enum logic [N-1:0] { ... }` with `'pos` values | preserve literal names; enum-indexed arrays need index casts |
| record | `typedef struct packed { ... }` | member order and bit order documented; unconstrained record members are an error until supported |
| constrained array | packed array if sliceable, unpacked array if memory-like | slicing an unpacked array is not directly expressible; lower slices to loops/temporaries |
| unconstrained array port | unpacked array port (SV) or specialized port | MVP may specialize; structural mode uses SV array ports |
| signal `<=` | `<=` | NBA semantics |
| variable `:=` | `=` | process variables persist across activations; declare them non-automatic |
| process with clock edge | `always_ff` | recognize `rising_edge`/`falling_edge`/`'event and clk = '1'` idioms; async reset patterns -> `always_ff @(posedge clk or posedge rst)` |
| `process(all)` | `always_comb` | VHDL-2008 process-all |
| process with explicit sensitivity | `always @(...)` | preserve the list; do not silently convert an incomplete list to `always_comb` |
| concurrent assignment | `assign` | conditional -> `?:` |
| selected assignment | `always_comb case` | or a ternary chain where clearer |
| `if`/`case`/static `for` | `if`/`case`/`for` | preserve statement structure |
| function/procedure | `function`/`task` | mangle overloads; inline only for unconstrained/aggregate signatures SV cannot express (document each case) |
| generic constant | `parameter`/`localparam` | specialize in MVP; structural mode emits parameters |
| generic type | `parameter type` | structural mode only |
| generic string | `parameter string` | structural mode only |
| `for generate` | `generate for` | labels become block names |
| `if generate` | `generate if` | static condition required |
| package/use | `package`/`import` | deferred constants -> `localparam`; package initialization order documented |
| `'left`/`'right`/`'high`/`'low`/`'length`/`'ascending`/`'range` | constant folding | prefer folding; emit source-location comments |
| `'event`/`'stable` | clock idiom recognition, else `$rose`/`$stable` | synthesis-oriented; do not invent behavior for non-synthesis uses |
| `assert`/`report` | optional SV assertion or drop with a comment | translation mode decides; default is drop with a warning for synthesis |
| `'Z'`, `inout` | `1'bz`, tri-state net | the text emitter is not bound by the Verilator `V3Tristate`/DFG limitation |
| multi-driver resolved `std_logic` | multiple drivers on a `wire` | only the `std_logic` family; user resolution functions are an error |
| user resolution function | error | no general SV equivalent |
| `wait until <clock edge>` | known idioms only | otherwise error; no `--timing` translator |
| `after`, `wait for` | error | not synthesizable RTL |
| access/protected/file/physical/textio | error | explicit unsupported diagnostic |
| PSL | out of scope | |
| configuration | out of scope initially | default binding only |

### 4.3 Semantic decisions to document in the emitted file

- 4-state vs the frontend's 2-state simulation model: the translator emits `logic`;
  literal emission must not rely on the simulation path's collapse.
- Index/ranges: preserve VHDL direction and index origin in the emitted declaration where
  SV allows it. Normalize only where an expression would otherwise change meaning.
- Packed vs unpacked arrays: decide per type, not per use; a slice of an unpacked array
  requires a lowering step.
- Integer width: subtype range -> vector width; plain `integer` -> 32-bit signed.
  Arithmetic overflow and comparison semantics must be differential-tested.
- Process variables: persistent, non-automatic; initial values at elaboration time.
- Signal initial values: declaration initializer or `initial`; note that ASIC synthesis
  may ignore them.
- Resolution: only the standard `std_logic` resolution; anything else is rejected.
- Naming: original VHDL spelling preserved; case-folded lookup only in the frontend.
  Overload mangling must be deterministic and documented.

### 4.4 Output quality rules

- No Verilator-internal names or constructs in the output (`__V`, `vhdlind_`, `$root`,
  `continuous assign`, internal temporaries).
- One emitted `always` per VHDL process when the process is representable as one process;
  split only for multiple clocks/disjoint targets, and say why in a comment.
- Emit `// VHDL: file:line` before each declaration/statement group.
- Deterministic ordering and formatting for reviewable diffs.
- One output file per entity/package by default; a single-file mode may follow.

## 5. Work packages

| WP | Deliverable | Depends | Size |
|---|---|---|---|
| WP-T0 | Frontend decision gate (Section 0); freeze the model interface | integration | S |
| WP-T1 | `VhdlDesign` model + `VhdlDesignFromXml`; extract semantic helpers; existing lowering ported to the model with unchanged `t_vhdl_*` results | T0 | M-L |
| WP-T2 | Emitter skeleton: writer, naming, comments, file layout, one module per entity, ports/declarations | T1 | S-M |
| WP-T3 | Core RTL: concurrent assignments, processes, if/case/for, variables, initial values, clock/reset idioms | T2 | M |
| WP-T4 | Types and aggregates: logic/bit/integer/enums, arrays, records, aliases, conversions, attributes, slices | T3 | L |
| WP-T5 | Subprograms and packages: overload mangling, functions/tasks, package/import emission, inlining fallback | T4 | M |
| WP-T6 | Hierarchy: instantiation, component binding, generics (specialized first), generate emission | T3 | M |
| WP-T7 | Verification: lint, structural goldens, differential simulation, corpus runner | continuous | M |
| WP-T8 | Hardening: diagnostics, unsupported-feature policy, performance, docs | continuous | S-M |
| WP-T9 | Optional: structural generics/generate; restricted Verilog-2001 mode | T4-T6 | M each |

Sequencing notes:

- T2 can start on a narrow, hand-written model fixture before T1 is complete, but the
  fixture must be replaced by the real model before merge.
- T4 is the risk centre: packed/unpacked arrays, slicing, records with unconstrained
  members, enum-indexed arrays, and integer ranges. T5 and T6 are parallelizable after
  the model is stable, but shared files need one owner.
- The front half is already paid for. This is a second backend, not a second frontend; do
  not budget for a new parser unless WP-T0 chooses that path.

## 6. Verification

### 6.1 Layers

| Layer | What | How |
|---|---|---|
| Translator unit | constructs emit expected SV | small `.vhdl` -> `.sv` golden files, whitespace-normalized |
| Re-parse | emitted SV is legal and lint-clean | `verilator --lint-only` on the emitted SV; ideally a second tool (Yosys/slang) |
| Functional | emitted SV equals the VHDL simulation | same C++ testbench or random vectors against the existing VHDL-in-Verilator path; compare on 0/1, and X/Z where defined |
| Structural | hierarchy, names, process count, types preserved | inspected goldens for representative designs |
| Corpus | accepted designs translate | extend `nodist/ghdl_synth_suite.py` with a translate-and-lint mode over the PASS bucket |
| Negative | unsupported constructs rejected | `t_vhdl_xlate_*_bad` with exact diagnostics and VHDL locations |
| Determinism | same input -> same output | sort declarations and modules; no hash-order iteration |

### 6.2 Acceptance criteria

- Every design that the current frontend accepts for simulation either translates or is
  rejected before any output is written. No silent partial output.
- Emitted SV re-lints cleanly in Verilator (warnings either fixed or explicitly waived in
  the test, not in the product).
- Differential simulation matches on the demo corpus and on the `t_vhdl_*` designs whose
  testbenches are reusable.
- Emitted SV contains no Verilator-internal identifiers or debug-emitter constructs.
- Existing `t_vhdl_*` simulation results are unchanged by the model refactor.
- Unsupported constructs each have a test and a documented loss.

### 6.3 Oracle choice

Use the existing VHDL-in-Verilator path as the primary functional oracle and GHDL
simulation as the secondary oracle for 4-state/X cases. Do not use `ghdl --synth` output
as the oracle; it is a different product and its netlist structure is irrelevant to RTL
translation.

## 7. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Model refactor changes simulation behavior | regressions in the shipped frontend | pure-refactor commits, AST dump comparison, full `t_vhdl_*` suite after each step |
| RTL emitter drifts from lowering semantics | two answers to the same question | single model; one direction of dependency; shared elaboration results |
| GHDL XML drift | translator breaks on a new GHDL | pin a version, version-check at startup, keep XML knowledge in one builder, liberal parsing |
| Aggregates and slicing | large, late work | decide packed/unpacked policy early; build T4 tests before T5/T6 |
| Process forms | incorrect splitting or unnecessary errors | reuse the current clock-idiom recognition; split only when targets are provably disjoint; otherwise reject |
| Output not accepted by downstream tools | product unusable | target SV-2017, lint with more than one tool where available, provide a conservative flattening fallback for specific types |
| Scope creep into netlist/synthesis | duplicate `ghdl --synth` | explicit non-goals; review any "optimize the netlist" change against Section 1.2 |
| Formatting/goldens churn | review noise | goldens normalized; test semantics first, structure second |

## 8. Open decisions

1. Separate `verilator_vhdl2v` binary vs a `verilator --language vhdl --emit-sv` mode.
   Recommendation: a separate entry point that shares the frontend, because the success
   criteria, defaults and output layout differ from a simulation run.
2. Specialized generics first vs structural parameters first. Recommendation:
   specialized first; structural after T4-T6.
3. Integer mapping: subtype range as vector width vs 32-bit `int`. Recommendation:
   subtype range for portability with synthesis semantics; differential tests decide.
4. Records as packed structs vs flattened. Recommendation: packed structs in SV; flatten
   only in the optional Verilog-2001 mode.
5. Arrays as packed vs unpacked vs flattened. Recommendation: packed when all dimensions
   are sliceable; unpacked for memory-like arrays; lower slices of unpacked arrays.
6. Subprograms: emit tasks/functions vs inline. Recommendation: emit by default, inline
   only for signatures SV cannot express; keep a per-case comment.
7. Assertions: drop with warning vs emit SV assertions. Recommendation: drop for
   synthesis by default, optional emission later.
8. Resolution: `std_logic` multi-driver only vs broader. Recommendation: `std_logic`
   family only; reject user resolution functions.
9. Whether the translator ever targets Verilog-2001. Recommendation: not until a concrete
   user exists.
10. Whether the model is a full IR or a narrow facade. Recommendation: full enough for the
    emitter; do not over-model constructs neither consumer needs.

## 9. Prior art and positioning

| Tool | Output | Difference |
|---|---|---|
| `ghdl --synth --out=verilog` | Verilog netlist | netlist, not RTL; the motivation for this plan |
| `ghdl-yosys-plugin` | RTLIL for Yosys | synthesis front end, netlist after mapping |
| vhd2vl | Verilog-2001 RTL | own parser/semantics; limited VHDL coverage; the semantic front half is the hard part |
| HDLconv | wrappers around GHDL/Yosys | netlist-oriented |
| Verific | commercial VHDL/Verilog frontend | licensing and integration differ; netlist/RTL depends on the flow |

The niche is an RTL-preserving translator with GHDL-grade semantic analysis (resolved
overloads/types) and a maintainable open-source implementation. That niche is real, but it
is only reachable if the output is legal, readable, structure-preserving SystemVerilog; a
flattened Verilog-2001 output would not differentiate from `ghdl --synth`.

## 10. Suggested file layout

| File | Responsibility |
|---|---|
| `src/V3VhdlDesign.{h,cpp}` | model types, invariants, naming |
| `src/V3VhdlDesignXml.{h,cpp}` | build `VhdlDesign` from `V3VhdlXml` (today) |
| `src/V3VhdlDesignAst.{h,cpp}` | build `VhdlDesign` from an in-tree frontend (if WP-T0 replaces GHDL XML) |
| `src/V3VhdlElab.{h,cpp}` | elaboration extracted from the current frontend: bindings, generics, generate, port specialization |
| `src/V3VhdlEmitSv.{h,cpp}` | SystemVerilog RTL emitter (do not extend `V3EmitV`) |
| `src/V3VhdlTranslate.{h,cpp}` | driver: model -> emitter, output files, diagnostics |
| `test_regress/t/t_vhdl_xlate_*.{py,vhdl}` | translation tests and differential harness |
| `nodist/ghdl_xlate_suite.py` | corpus translate-and-lint runner |

Build wiring follows the existing pattern: `RAW_OBJS` in `src/Makefile_obj.in`,
`src/CMakeLists.txt`, and the header wildcard. The translator is single-threaded like the
frontend (`VL_MT_DISABLED`).

## 11. Milestones

| Milestone | Content | Exit criterion |
|---|---|---|
| M-T0 | Gate + model interface | Frontend choice recorded; model interface reviewed |
| M-T1 | Model refactor | `t_vhdl_*` simulation unchanged; lowering consumes the model |
| M-T2 | Demo translator | one clock domain, vectors, concurrent/sequential logic, no aggregates -> lint-clean SV that simulates equal |
| M-T3 | Types | arrays, records, enums, integer ranges, attributes -> corpus designs translate and lint |
| M-T4 | Subprograms/packages/hierarchy | functions/tasks, packages, instances, specialized generics, generate |
| M-T5 | Solid subset | corpus PASS bucket translates or rejects cleanly; differential equivalence on the demo corpus |
| M-T6 | Optional | structural generics/generate; Verilog-2001 mode |

## 12. What not to do

- Do not extend `V3EmitV`; it is a debug emitter and its gaps are intentional.
- Do not emit from the post-width AST; the structure is already gone.
- Do not build a netlist translator; `ghdl --synth` owns that.
- Do not re-implement VHDL parsing unless WP-T0 chooses to replace the frontend.
- Do not let the emitter read `V3VhdlXmlNode` directly; keep the model boundary.
- Do not optimize the emitted RTL for area/timing; this is translation, not synthesis.
