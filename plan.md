<!-- SPDX-FileCopyrightText: 2026 Hannes Klas
     SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0 -->

# VHDL frontend acceptance audit -- fix plan

**Status:** planning document for the 2026-11-20 audit. It is not user
documentation; the assessment in `docs/vhdl/review.rst` is the user-facing
summary.

**Scope:** the findings in `docs/vhdl/review.rst` under "Open audit findings".
The feature scope remains `vhdl_synth_subset.md`; where the audit shows the
subset and the implementation disagree, the decision is recorded in the work
package.

**Ground rules**

- Every acceptance fix is a frontend fix: no new V3 node, pass, or runtime
  primitive unless the work package says so explicitly.
- An illegal VHDL program gets `v3error("VHDL: ...")`; a construct outside the
  subset gets `unsupportedError`. Do not use `unsupportedError` for a user
  mistake such as an unknown type or a bad index. (See WP13 for the message
  form.)
- Every new diagnostic gets a `_bad`/`_unsup` golden, per the repository
  `AGENTS.md` table. A fix that changes a model needs the value compared with
  GHDL (and nvc where GHDL is lenient).
- One work package per commit. Keep the parser, lowering and test changes of a
  package together.
- ASCII only in C++ sources and headers. Use `--`, not an em dash.

## Priority and sequencing

| WP | Area | Severity | Depends on |
|---|---|---|---|
| 1 | Nested real/physical element types (crash) | P0 | -- |
| 2 | Function used as a statement / procedure used as a value | P0 | -- |
| 3 | Port actual, default and missing-input conformance | P0 | -- |
| 4 | Entity architecture selection and configuration specifications | P0 | -- |
| 5 | Component declaration conformance | P1 | 3, 4 |
| 6 | Expression legality: chains, mixed array types, character literals | P1 | -- |
| 7 | Case/selected selector and empty-case legality | P1 | -- |
| 8 | Declaration, label and severity legality | P1 | -- |
| 9 | Static fold bounds and remaining scalar checks | P2 | -- |
| 10 | Null arrays: decide and enforce | P2 | -- |
| 11 | Under-acceptance: legal forms the parser refuses | P2 | -- |
| 12 | `--top-module` case folding | P3 | -- |
| 13 | `Unsupported:` diagnostic form and golden sweep | P3 | all |

WP1, WP3 and WP4 change models or crash. Land them first. WP5 needs the port
conformance helper from WP3 and the architecture selection from WP4. WP13 is a
mechanical sweep and should be last so it does not rebase every other commit.

**Status:** WP1--WP4 are implemented and tested in this tree. The remaining
work packages are open.

## WP1 -- Nested real and physical element types

**Symptom.** `rejectUnmodelledObject` (`src/V3VhdlNativeLower.cpp:2072`) checks
only the top-level `VType`. An object of an array or record whose element/member
is `real` or a physical type is accepted, and the first use segfaults:

```vhdl
type rv_t is array (0 to 3) of real;
signal r : rv_t;
y <= integer(r(0));        -- SIGSEGV
```

`r(0) < r(1)` and `integer(r.f)` for a record field crash the same way.
Backtrace: `AstNodeDType::width(this=0)` from `typeOfDType(0)` at
`V3VhdlNativeLower.cpp:565`, reached from `lowerConversion`/`lowerBinary`
through `typeOfLowered`.

**Change.** Make the gate recursive:

- Add a helper that walks `VType::elemp` for arrays/vectors and `VType::fields`
  for records (with a depth guard against recursive types; none should exist in
  this subset, but assert it).
- Reject a `real` element/member with
  `unsupportedError(flp, "real object element")`.
- Reject a physical (`LONGINT`) element/member with
  `unsupportedError(flp, "physical object at run time")`.
- Call the recursive check from every object site that already calls
  `rejectUnmodelledObject`: ports, signals, constants, variables, aliases,
  subprogram locals, generate/block declarations, and port bounds.
- As a defensive measure in the same change, make `lowerConversion` and
  `typeOfLowered` report a frontend error instead of dereferencing a null dtype;
  the gate should make it unreachable, so this is one assertion, not a new path.

**Tests.** `t_vhdl_nested_real_unsup.vhdl`: an array of `real`, a record with a
`real` field, and both a conversion and a relational use. The golden must be a
clean `%Error`, not "internal fault". Add a second case for an array of `time`.
Optionally keep a positive case proving an array of `integer` and a record of
`integer` still work.

**Exit.** Both reproducers and the `time` variant report a source-located error;
`--lint-only` and `--cc` agree; no release-build crash.

## WP2 -- Subprogram class mismatch

**Symptom.** `resolveCall` filters declarations by function/procedure class and
returns `nullptr` when declarations exist but none matches, because `firstWhy`
stays empty (`src/V3VhdlNativeLower.cpp:8252-8262`). The caller then drops the
statement or the assignment silently:

```vhdl
function f(x : integer) return integer is ...
f(a);                 -- accepted, dropped
```

```vhdl
procedure p is begin null; end procedure;
y <= p;               -- accepted, y has no driver
```

The no-parentheses name path at `V3VhdlNativeLower.cpp:3105` also converts any
subprogram name into a zero-argument call and hits the same null return.

**Change.**

- In `resolveCall`, when `refs` is non-empty and `matching` is empty, compute a
  `firstWhy` from the class mismatch: "name designates a function, not a
  procedure" or the reverse. Report it with `v3error` (illegal VHDL) and return
  `nullptr`.
- In the NAME-without-parentheses path, only build a CALL when a visible
  zero-argument function exists. If only procedures exist, report "procedure
  used in an expression". Otherwise fall through to the existing unknown-name
  error.
- Keep overload resolution across both classes: a legal call with a procedure
  and a function of the same name must still select by context.

**Tests.** `t_vhdl_subp_class_bad.vhdl`: function call as a sequential
statement, as a concurrent statement, and a procedure name on the right-hand
side of an assignment. Golden covers all three. Add a positive check that a
function and a procedure sharing a name still resolve.

**Exit.** No statement or assignment is ever dropped because a subprogram of the
wrong class was found; no output is left undriven.

## WP3 -- Port actual, default and unbound-input conformance

**Symptom.** Three related holes:

1. The whole-port conformance check only compares shapes when both sides are
   vectors (`src/V3VhdlNativeLower.cpp:13056-13075`). A scalar actual of any
   type is accepted and coerced: an `integer` signal into a `std_logic` port, a
   `boolean` signal into `std_logic`, `a => 5`.
2. An unassociated named input without a default, and `a => open` on such an
   input, creates a pin with a null actual and no diagnostic
   (`V3VhdlNativeLower.cpp:12966`).
3. A port default is lowered without any conformance check
   (`declaredDefault`, `V3VhdlNativeLower.cpp:12896`), so
   `port (a : in std_logic_vector(3 downto 0) := "11")` is resized silently.
   Generic defaults have the same shape hole.

**Change.**

- Replace the ad-hoc vector comparison with `valueConforms`:
  `valueConforms(portType, *assoc.actual, "port actual")`, then `resizeExpr`.
  `valueConforms` already checks base type for scalars and vectors and shape for
  names/slices. Keep the record-member and partial-port paths separate.
- In the `assocs.empty()` path: call `declaredDefault`; if it returns null and
  the port is an input, report "input port '<name>' has no default value and is
  not associated". The same check applies to an explicit `open` association on
  an input, with a message naming `open`.
- Run `valueConforms` on the lowered default before `resizeExpr`, using the
  port's type. For an unconstrained port, use the specialized type.
- Apply the same conformance check to generic defaults (entity and package)
  and to entity/package generic actuals with shape checking. An 8-bit string
  actual to a 4-bit vector generic is an error.
- Keep `open` on an output legal, and keep an unassociated output legal.
- Add a test that a missing input *with* a default still elaborates and uses
  the default, and that an unassociated input with a default is not an error.

**Tests.**

- `t_vhdl_port_scalar_bad.vhdl`: integer and boolean actuals, integer literal,
  wrong default type. Golden lists each port.
- `t_vhdl_port_unbound_bad.vhdl`: named omission and `open` without a default.
- `t_vhdl_generic_shape_bad.vhdl`: vector generic actual and default widths.
- Positive: `t_vhdl_port_default.vhdl` for a defaulted input, and an existing
  `t_vhdl_unconst_port` run to prove specialization still works.

**Exit.** Every mismatch GHDL rejects is rejected; every legal default/open
association is still accepted; no new false positive on the existing port tests.

## WP4 -- Entity architecture selection and configuration specifications

**Symptom.** `parseInstance` parses and discards `(ARCH)`:
`src/V3VhdlNativeParse.cpp:1845`. The model always uses the last-analyzed
architecture, so `entity work.a1s(rtl)` can build different hardware than the
source says. A configuration specification inside an architecture is skipped
wholesale (`src/V3VhdlNativeParse.cpp:2112`), so
`for u : a2 use entity work.a2(rtl);` is ignored and the default architecture is
used. `disconnect` shares that skip path and is tier `X` in the subset.

**Change.**

- Add `architectureName` to `Concurrent` (instance). Parse the selected name
  and keep it; keep the `-` form as "any".
- When lowering an instance, select the architecture by name for the entity.
  Error if it does not exist: "VHDL: entity 'x' has no architecture 'y'".
  Beware of the existing rule that the last-analyzed architecture is the
  default when no selection is given.
- Decide configuration specifications. Recommended: refuse them with
  `unsupportedError(flp, "configuration specification")`, because the subset
  does not list them and the configuration design unit is already tier `D`.
  Do the same for `disconnect` with its own message instead of silently
  skipping it. If a later milestone implements configuration specifications,
  the parser should keep the binding, not drop it.
- Do not change the configuration design unit's validate-not-apply behavior in
  this package; that is a separate, documented limitation.

**Tests.**

- `t_vhdl_inst_arch.vhdl`: two architectures returning different constants,
  instantiate the first by name, check the value with a testbench or the
  existing C++ harness pattern used by `t_vhdl_config`.
- `t_vhdl_inst_arch_bad.vhdl`: nonexistent architecture selection.
- `t_vhdl_cfg_spec_unsup.vhdl`: the architecture-level `for u : c use ...`
  form, and `disconnect`, each with a clean refusal.
- Re-run `t_vhdl_config` and `t_vhdl_config_comp_bad`; the configuration unit
  behavior must not change.

**Exit.** The selected architecture is the one in the model; a missing
selection is an error; no configuration specification is silently ignored.

## WP5 -- Component declaration conformance

**Symptom.** `parseComponentDecl` drops the whole declaration
(`src/V3VhdlNativeParse.cpp:1800`). Instantiation formals are checked against
the bound entity, not the component. A stale component with a different port
list therefore builds in Verilator and fails in GHDL.

**Change.**

- Give the parser a `ComponentDecl` record: name, generic clause and port
  clause, using the same `GenericDecl`/`PortDecl` structures. Store it in the
  enclosing architecture/package/generate/block scope.
- At instance binding:
  1. If a component of the instance's name is visible, validate the component
     interface against the bound entity (same port names, directions and
     subtype base types; same generic names and types).
  2. Check the association formals against the component's ports. An unknown
     formal is a user error; a whole-port double association is already
     caught.
  3. Then build the pin list from the entity's ports as today.
- A direct entity instantiation (`entity work.e`) has no component declaration;
  keep the current path.
- Keep component declarations legal and droppable when unused; the new work is
  validation at the point where binding information exists.

**Tests.**

- `t_vhdl_component_conform_bad.vhdl`: component port renamed and component
  port type changed; both must fail.
- `t_vhdl_component_unknown_bad.vhdl`: association names a port the component
  does not declare.
- Positive: `t_vhdl_component` and `t_vhdl_config` must still pass.

**Exit.** A component declaration and the bound entity must agree before the
instance is emitted; GHDL and Verilator agree on the accepted set.

## WP6 -- Expression legality: chains, mixed array types, character literals

**Symptom.** Three independent gaps:

- Relation, shift and unary operators loop without the non-associativity rule
  that the logical level already enforces (N113). `a = b = c`, `a sll 1 sll 2`
  and `not not a` are accepted (`src/V3VhdlNativeParse.cpp:558,577,644`).
- Logical, concatenation and arithmetic operators accept arrays of unrelated
  base types when the element kind matches: `unsigned + std_logic_vector`,
  `unsigned and std_logic_vector`, `unsigned & std_logic_vector`.
- `valueConforms` treats every character literal as untyped, so a logic
  character becomes an integer/real/boolean: `integer_signal = '1'`,
  `return '1'` from an integer function, `generic(n : integer := '1')`.

**Change.**

- Parser: in `parseRelation` and `parseShift`, reject a second operator at the
  same precedence level unless the first operand was parenthesized. In
  `parseUnary`, require a primary after a unary operator (so `not not a` and
  `- - a` fail, while `not (not a)` passes). Move the existing logical rule to
  one helper so all four levels share it.
- Logical/concat: compare base type **names** for arrays, not only element
  kinds. Keep the documented VHDL-2008 scalar/array mix and the
  `std_logic`/`std_ulogic` family relation. `unsigned` and
  `std_logic_vector` are unrelated.
- Arithmetic: require the same base type for two vector operands. Keep the
  existing int-to-vector and package rules (`numeric_std` overloads,
  `std_logic_arith` widening, `std_logic_signed` refusal).
- Character literal: in `valueConforms` and in the literal lowering, allow a
  character literal to adopt only a logic/bit/character/enumeration type, not
  `integer`, `real`, `boolean`, or a physical type. In
  `relationalOperandsConform`, a `CHAR_LIT` may pair with a logic/bit scalar, a
  character, or an enum that declares that literal; otherwise report a type
  mismatch.

**Tests.**

- `t_vhdl_op_chain_bad.vhdl`: relation chain, shift chain, unary chain, with a
  positive parenthesized form beside each.
- `t_vhdl_mixed_array_op_bad.vhdl`: the three operator families.
- `t_vhdl_char_ctx_bad.vhdl`: relational, return and generic-default cases.
- Re-run the operator and type-conformance tests.

**Exit.** GHDL's accepted set and Verilator's agree for these families; no
existing operator test regresses.

## WP7 -- Case and selected-assignment legality

**Symptom.** An empty `case` statement is accepted and dropped (the
`itemsp == nullptr` return in `lowerCase`, `src/V3VhdlNativeLower.cpp:7315`).
The selector type is never required to be discrete or a one-dimensional array
of a character type, so `case r is when others =>` over a record builds.
`with r select ... when others` has the same selector hole.

**Change.**

- In `lowerCase`, report "case statement has no alternatives" when there are no
  items.
- Validate the selector type once, before the item loop: discrete scalar, or a
  one-dimensional array whose element is a character enumeration
  (IEEE 1076-2008 10.9). Use the same helper for `with ... select`.
- Keep the existing completeness and duplicate checks; they run after a
  non-empty item list.

**Tests.** `t_vhdl_case_empty_bad.vhdl`, `t_vhdl_case_selector_bad.vhdl`
(record and integer-array selectors, case and selected assignment). Keep
`t_vhdl_case`, `t_vhdl_slv_compare`, and the case coverage tests green.

**Exit.** Every case/select GHDL rejects is rejected before lowering; no
accepted case loses its completeness check.

## WP8 -- Declaration, label and severity legality

**Symptom and change.**

- Plain `variable` in an entity or architecture declarative part is accepted;
  only `shared variable` is legal there. Reject it in `parseEntity` and
  `parseArchitecture`; keep the `shared variable` path.
- Process/block/generate statement labels are not registered, so duplicate
  labels and a label colliding with an object are accepted. Add a per-region
  label set to the parser/lowering scope and report a duplicate at the second
  declaration. Instance labels already have this check; reuse its wording.
- `severity <token>` accepts any identifier as `note`
  (`Parser::parseSeverityValue`, `src/V3VhdlNativeParse.cpp:932`). Require one
  of `note`, `warning`, `error`, `failure`; anything else is a parse error.
- A package body without a package specification is accepted. Track package
  specifications and report an orphan body.
- Duplicate subprogram bodies with the same designator and profile in one
  declarative region are accepted until called. Detect the duplicate at
  registration.
- The entity statement part is supposed to be passive; a process there that
  assigns a signal is accepted. Reject signal/variable assignment in an entity
  statement-part process (reads and concurrent assertions stay legal).

**Tests.** `t_vhdl_decl_scope_bad.vhdl` (variable in entity and architecture),
`t_vhdl_label_dup_bad.vhdl`, `t_vhdl_severity_bad.vhdl`,
`t_vhdl_pkg_body_orphan_bad.vhdl`, `t_vhdl_subp_dup_bad.vhdl`,
`t_vhdl_entity_passive_bad.vhdl`. Keep `t_vhdl_shared`, `t_vhdl_block`,
`t_vhdl_gen`, and `t_vhdl_entity_assert` green.

**Exit.** Each illegal form gets one source-located error; no legal shared
variable, label, or passive assertion is refused.

## WP9 -- Static fold bounds and remaining scalar checks

**Symptom and change.**

- Static integer overflow wraps silently: `integer'high + 1`, `2 ** 31`,
  `2 ** 40`. Add a bounds check wherever a folded `int64_t` becomes a 32-bit
  `integer` constant or a 32-bit result: if the value is outside
  `[-2**31, 2**31-1]` and the target is `integer` (not `time`/physical and not
  `LONGINT`), report "VHDL: integer overflow in a static expression". Keep the
  existing literal bound and N118 constant checks.
- `'length` on a scalar is accepted. Require an array prefix for `'length` and
  for the array-only attributes.
- A shift/rotate count that is a character or boolean is accepted
  (`v sll '1'`). Require an integer operand for the predefined shifts and for
  the `numeric_std` shift counts.

**Tests.** `t_vhdl_fold_overflow_bad.vhdl`, `t_vhdl_attr_length_bad.vhdl`,
`t_vhdl_shift_count_bad.vhdl`. Keep `t_vhdl_attr`, `t_vhdl_attr_scalar`,
`t_vhdl_num_bits`, `t_vhdl_shift_neg`, and `t_vhdl_physical` green.

**Exit.** Static overflow is reported once, at the fold site; `time` and
physical folds still work; array attributes and integer shift counts are
unchanged.

## WP10 -- Null arrays: decide and enforce

**Current state.** Null ranges (`std_logic_vector(0 to -1)`) are accepted as
objects and ports; whole-array assignment is a deliberate no-op, and the review
notes four zero-width corpus designs still reach the middle end. The subset
says zero-width arrays are tier `X`.

**Recommendation.** Split the two cases:

- Reject a null range at an object or port declaration (a static zero-width
  object), with `unsupportedError(flp, "zero-width object")`. This includes
  ports; it is the case the corpus hit.
- Keep the no-op behavior for a null *slice* or a whole-array assignment whose
  bounds are only known after elaboration, because those are legal VHDL and
  already handled (`t_vhdl_null_assign`, `t_vhdl_null_string`).

If the maintainers prefer to keep zero-width objects as a documented extension,
update `vhdl_synth_subset.md` instead and add a value test for a zero-width
port. Do not leave the subset and the implementation disagreeing.

**Tests.** `t_vhdl_null_port_bad.vhdl`, `t_vhdl_null_object_bad.vhdl`; keep
`t_vhdl_null_assign` and `t_vhdl_null_string` green.

## WP11 -- Under-acceptance: legal forms the parser refuses

Each item is independent; one commit can cover several small parser fixes.

1. **Architecture of a selected entity name.**
   `architecture rtl of work.e is` fails at `parseArchitecture`
   (`src/V3VhdlNativeParse.cpp:2034`). Accept a dotted entity name and keep the
   final component, as `parseInstance` and `parseConfiguration` already do.
   Test: `t_vhdl_arch_qual.vhdl`.

2. **Qualified subtype mark in an array index constraint.**
   `type arr is array (work.pkg.idx) of integer;` fails because
   `parseIndexConstraint` (`src/V3VhdlNativeParse.cpp:1599`) takes one
   identifier. Factor the mark parsing out of `parseTypeSpec` and use it here.
   Test: `t_vhdl_index_mark_qual.vhdl`.

3. **No-map component instantiation.**
   `u : comp;` is parsed as a concurrent procedure call and reported as
   "unknown subprogram". Add a deferred statement kind (or an instance with a
   no-maps flag) for a labelled bare designator; lowering resolves it to an
   instance when the name is a component/entity, otherwise to a procedure call.
   An unlabelled bare designator stays a procedure call. Test:
   `t_vhdl_inst_nomap.vhdl`, plus a negative case for an unknown name.

4. **Operator-symbol use clause.**
   `use work.pkg."+";` fails in `parseUseClauseInto` (`:2971`) because the
   declaration item must be an identifier. Accept a `STRING` token as the
   final item and import it under its operator text. Test: `t_vhdl_use_op.vhdl`.

5. **Integer type definitions.**
   `type small_t is range 0 to 7;` is refused. Implement it as an integer
   subtype with an explicit range, reusing the subtype/range machinery, or
   record it as a deliberate scope decision in `vhdl_synth_subset.md`. Test:
   `t_vhdl_int_type.vhdl` if implemented.

6. **`--top-module` case.**
   The emitted VHDL module name is folded, so `--top-module CaseTop` fails for
   `entity CaseTop`. Either emit the declared spelling (and accept both
   spellings in V3LinkCells) or fold the requested name when the design is
   VHDL-only. Test: `t_vhdl_top_case.py` passing the mixed-case spelling.

**Exit.** Every legal form moved from the rejected list has a positive test;
the deferred-statement change for no-map instantiation keeps concurrent
procedure calls working.

## WP12 -- `--top-module` case folding

This is item 6 of WP11; it is listed separately because it may touch
`V3LinkCells` rather than the frontend. Prefer a frontend-only fix (emit the
spelling the user wrote and make the top lookup fall back to the folded name).
Do not make Verilog/SV module lookup case-insensitive globally.

## WP13 -- `Unsupported:` diagnostic form and golden sweep

**Symptom.** `unsupportedError` emits
`%Error: VHDL <construct> is not supported yet`
(`src/V3VhdlNative.cpp:62`), while the repository convention and the VHDL docs
promise `%Error-UNSUPPORTED:`.

**Change.** Emit `flp->v3error("Unsupported: VHDL " + what)`. This changes the
error code and every `_unsup` golden, and the corpus classifier that counts
"is not supported yet". Do it last, in one mechanical commit:

- Regenerate every `test_regress/t/t_vhdl_*_unsup.out` and every `_bad.out`
  that contains the old text with `HARNESS_UPDATE_GOLDEN=1`.
- Grep the tree for the old string and update `docs/vhdl/usage.rst`,
  `docs/vhdl/review.rst`, `V3VhdlNative.h`, and the corpus notes.
- Do not change any message that is a user error; those should use `v3error`
  without "Unsupported:".

**Exit.** `%Error-UNSUPPORTED` appears for subset refusals only; goldens and
docs are consistent; the corpus classification is re-run if the corpus is
available.

## Verification

Per work package:

```sh
ulimit -S -v 8388608                 # soft limit; a bare ulimit -v breaks ASAN
cd /home/hannes/git_extern/verilator-native
make -j4 -C src
cd test_regress
VERILATOR_ROOT=/home/hannes/git_extern/verilator-native \
  python3 t/t_vhdl_<name>.py
```

For a golden:

```sh
touch t/t_vhdl_<name>_bad.out
HARNESS_UPDATE_GOLDEN=1 python3 t/t_vhdl_<name>_bad.py
```

Before a PR: the full VHDL suite.

```sh
VERILATOR_ROOT=/home/hannes/git_extern/verilator-native \
  python3 driver.py -j 4 --vlt t/t_vhdl_*.py
```

References used for the audit: GHDL
`/home/hannes/.local/bin/ghdl -a --std=08` and
`/home/hannes/.local/bin/nvc --std=2008 -a`. Every negative test should have a
GHDL-confirmed reproducer in its description; a construct GHDL also accepts is
a frontend extension, not a bug, and needs a subset-doc note instead.

## Documentation updates

- `docs/vhdl/review.rst`: the "Open audit findings" section is the source list
  for this plan. Move a row to the closed/known list when its WP lands.
- `vhdl_synth_subset.md`: update the tier rows that WP10 and WP11 decide
  (null objects, integer type definitions, configuration specifications,
  `--top-module` case). Also reconcile rows the implementation already
  exceeds: `wait until <general condition>`, `signal`-class subprogram
  parameters, and the `numeric_bit`/`std_logic_arith`/`std_logic_unsigned`
  package rows.
- `Changes`: do not edit; the maintainer updates it near release.
- `docs/CONTRIBUTORS`: do not edit (humans only).

## Risks

- **Port conformance tightening** can reject designs that rely on the current
  coercion. Compare the new diagnostics against the GHDL corpus before landing;
  an accepted design that GHDL accepts must stay accepted.
- **Architecture selection** touches instance lowering and the existing
  "last analyzed wins" rule. Keep the default path bit-identical when no
  `(ARCH)` is written.
- **Label and package-body checks** are parser-scope changes and can affect
  inlining and package publication. Add the positive tests in the same
  package.
- **WP13 touches many goldens.** Do not mix it with behavior changes; a
  rebase of any earlier WP would otherwise conflict in every `.out` file.
