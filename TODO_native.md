<!-- SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
     SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0 -->

# Native VHDL frontend TODO

State on 2026-11-13, branch `vhdl-native-frontend`, commit `77153db` (P1 complete; arrays of
unconstrained vectors constrained at use now lower).

## Baseline

- `VERILATOR_TEST_VHDL_FRONTEND=native test_regress/driver.py -j 4 --vlt t/t_vhdl_*.py`
  -> **93 passed, 0 failed, 1 skipped** (the skip is `t_vhdl_ghdl_bad`, a GHDL-only
  test). Native regression parity reached: P0 and P1 are complete.
- `nodist/ghdl_synth_suite.py --frontend native --suite synth --jobs 8`
  -> **PASS 823, UNSUPPORTED 159, ERROR 97, EXPECTED_FAIL 39, NO_TOP 21**
  (up from PASS 614 / UNSUPPORTED 204 / ERROR 264 after the P2 parser work below;
  original baseline 542 / 244 / 300; 1139 cases, 28 dirs skipped).
  (GHDL XML frontend on the same corpus: PASS 741, UNSUPPORTED 232, ERROR 31).
  Corpus file selection now mirrors GHDL: `ghdl_synth_suite.py` appends the sibling files
  declaring packages that a case imports (`use work.<pkg>`) so a design is analyzed with the
  packages it `use`s. Previously a looped `synth -e $t` testsuite hid the package file, so
  many `type 'X' is not supported` results were an under-compilation artifact, not a frontend
  gap (that correction alone moved PASS 757->788, and ERROR never rose).

The native frontend now matches the GHDL XML path on the regression suite. The
corpus `ERROR` bucket has fallen from 300 to 101. Accepted or cleanly reported
since the baseline: PSL directives (`default`/`cover`/`restrict`/`assume`), PSL
assertions and `vunit`; attribute specifications; configuration `for ... use`
specs; real, exponent (`25e6`) and physical/time literals; `process(all)`; `entity
work.X(ARCH)` selection; `signal`/`constant` interface keywords; comma-separated
`use` clauses and computed array index bounds; and multi-dimensional array types,
interface views, external names, generic-package instantiation, record-member port
associations and subprogram actual names now report `unsupported` rather than a
parse error. The remaining `ERROR`s are genuine library/elaboration gaps (e.g.
`std_logic_arith` functions, operator overloading, `'subtype`) plus
two internal faults.

## P0 - correctness

- [x] `t_vhdl_split` miscompiled (`qe=1 expected 0`).
  A process with a single sensitivity signal (`process (clk)`) must run on every
  event of that signal. The lowerer now makes that process change sensitive and
  lowers `rising_edge`/`falling_edge` to the clock's level; a process with several
  sensitivity signals compares each edge signal with a per-process previous-value
  variable. Both are exact for the VHDL process model.

## P1 - native regression parity (complete)

- [x] `t_vhdl_conv`: concurrent/sequential aggregate assignment targets
  (`(a, b) <= value`, leftmost = MSB), unary reduction operators, operator-symbol
  calls (`"or"(a)`) and `*_reduce` intrinsics. Targets parse with `parsePrimary()`,
  so `<=`/`:=` are not eaten as binary operators.
- [x] `t_vhdl_gen`: package instantiation `package p is new work.pkg generic map
  (...)` and generic-package clauses, skipped via `skipToSemicolon()`.
- [x] `t_vhdl_math_real`: locally static `math_real` (`ceil`, `floor`, `log2`,
  `real`, ...) in constant ranges via `evalReal` + a pre-scan of integer constants.
- [x] `t_vhdl_num_bits`: static evaluation of a package function with a `for`
  loop / early `return` used in a subtype range (`staticIntFold` = lower, constify,
  fold); user subprograms now resolve before standard intrinsics.
- [x] `t_vhdl_port_slice`: unconstrained array ports and individual slice
  associations, specialized per instance from the actual's range.
- [x] `t_vhdl_record_sub`: constrained record subtypes
  (`subtype rec_a is rec_t(x(3 downto 0));`) widening elements in place, and
  record port specialization from the actual's member layout.
- [x] `t_vhdl_static_expr`: positional vector aggregates whose bit-literal
  arguments span multiple elements are concatenated.
- [x] `t_vhdl_string_generic`: string generics, per-binding specialization,
  character case choices.
- [x] `t_vhdl_typegeneric`: generic type parameters (`generic (type T; ...)`) with
  the binding's constrained type substituted into the entity's type table.
- [x] `t_vhdl_unconst_port`: port defaults (`d : in std_logic_vector := x"a5"`)
  and unconstrained entity port specialization; unconstrained-port entities
  elaborate only through instances, and a top-level one reports at the port.

## P2 - parser acceptance (corpus `ERROR` bucket, highest count first)

Cheap acceptance first: these constructs are synthesized away or only need to be
parsed/skipped. The corpus `ERROR` count is now 103 (was 300).

- [x] PSL directives `default clock is rising_edge(clk);`, `default ...;` and `{...}`
  property blocks, at design-unit, entity, package, architecture-declarative and
  concurrent level; the lexer now emits `{ } [ ]` as symbols so the skips work.
- [x] Multi-dimensional array types `array (0 to 1, 3 downto 0)` with static index
  constraints lower as nested packed `VType`s (`registerType` folds each index into a level;
  `lowerSel` descends one array level per index, so `m(i)(j)` and `m(i, j)` both work).
  `t_vhdl_array2d` (mixed-direction 2-D, row-slice + scalar access, value-checked) added.
  Still `unsupported`: comma object-level subtype `sig : t(r1, r2)` and comma multi-index
  unconstrained types `array (natural range <>, natural range <>)` (the parse stays UNSUPPORTED,
  so those cases did not regress to ERROR). Corpus +3 PASS (issue1220/1406/1505). Paren nested
  constraints `t(r1)(r2)` on arrays of vectors are now supported -- see the P3 item.
- [x] Qualified `lib.pkg.type` marks collapse to their final identifier (relying on
  the `use` clause); `t(...)(...)` multi-dimensional index constraints report
  `unsupported`.
- [x] Declarations with real/exponent (`25e6`) and physical/time (`27.777 us`) literals:
  an exponent real folds to an integer when integral; physical/time literals now lower
  (see the physical-literal item below).
- [x] Architecture selection on instantiation `entity work.X(RTL)`; the identifier
  is parsed and ignored (the frontend keeps one architecture per entity).
- [x] Generic-package instantiation `package X is new G generic map (...)` at design-unit
  and architecture scope: a package with generics is a template (not elaborated on its own);
  each instance is elaborated with its generics bound and published under the instance name,
  so `use inst.all` and the package-body functions see the actuals (integer or type, named
  or positional); `t_vhdl_genpkg` added.
- [x] Generic *subprogram* formal parameters `generic (function f(a : T) return T)` on an
  entity: the formal's signature is parsed but only its name kept; an instance binds a named
  function actual and the specialization registers the formal as a call alias to that actual
  (`t_vhdl_genfunc`). A subprogram generic on a *package* reports `unsupported` (packages are
  lowered without an instantiation scope).
- [x] Positional/bound attributes used as an index or slice bound (`v(x'high)`,
  `v(x'high downto x'low)`, `v(1 to v'high-1)`): only `x'range` is a whole-array slice in
  `lowerSel`; any other attribute index or slice bound falls through to normal expression
  lowering, and `evalInt` already folds `'low`/`'high`/`'left`/`'right`/`'length`/`'pos`;
  `t_vhdl_attr_idx` added.
- [x] Signal attribute `'event` used as an operand -- parenthesized
  `elsif (clk'event and clk='1')`, the three-term clock enable
  `clk'event and clk='1' and en='1'`, and `clk'event and clk = CONST`: `lowerAttr`
  registers the signal as an `ET_CHANGED` sensitivity edge and reduces like
  `lowerEdgeCall` (no event at time zero, the level under a single-sensitivity always,
  else compare with the previous value); `stable`/`active`/`last_*` still unsupported,
  covered by the extended `t_vhdl_event`.
- [x] Logic literal `'Z` (scalar `'Z`, `others => 'Z'`, and `'z` inside a bit-string):
  `makeBitsConst` resolves a `'z` bit to logic 0 (high impedance has no driver in the
  two-state model), matching the already-`is_x`->0 stance; the `'Z'` unsupported guards in
  `lowerExpr`'s CHAR_LIT and AGG_OTHERS cases are dropped. Native-frontend only (the GHDL
  XML frontend rejects `'Z` at its own conversion). `t_vhdl_logic_z` added.
- [x] Concurrent assignment to an inout port (`io <= x`) now reports `unsupported` instead
  of crashing `V3DfgSynthesize` with a non-read-only reference -- the existing procedural
  guard never covered the concurrent path (`emitConcurrent` builds the assign directly and
  bypasses `lowerAssign`); `writesInoutPort` helper shared by both. Fixes a pre-existing
  crash reproducible with plain `io <= d`; `t_vhdl_inout_bad` extended with a concurrent case.
- [x] Logical operations (`and`/`or`/`xor`/...) on operands of "different types" -- the last
  big UNSUPPORTED bucket, now empty. Two distinct root causes, two commits:
  (a) integer-integer comparisons in `lowerBinary` returned the comparison node without a
  result dtype, so an int test mixed with a std_logic test in `and`/`or` saw mismatched
  kinds; they now set `bitDtp` like every other comparison (`t_vhdl_logical_intcmp`).
  (b) a scalar bit mixed with a vector (`vec and s`, IEEE 1164 / predefined bit_vector) is
  broadcast via `AstReplicate` to the vector width, guarded to a width-1 non-vector operand
  of the same BIT/logic family so a genuine length-mismatch (e.g. `vec(11) or "1"`) still
  errors (`t_vhdl_logical_scalar`). Resolves all 11 corpus cases; `issue2553` turns NO_TOP
  (harness looks up lowercase `debounce` but the entity is `Debounce` -- real compile clean).
- [x] Physical (time) literals -- the `physical literal` UNSUPPORTED bucket, now empty.
  `parsePrimary` turns `<numeric> <unit>` (fs/ps/ns/us/ms/sec) into a `PHYS_LIT` holding the
  value scaled to the physical type's step unit (femtoseconds, so 64 bits are needed, e.g.
  2 sec = 2e15 fs); `time` resolves to a 64-bit signed integer (`LONGINT`) and `typeOfDType`
  treats `LONGINT` as an integer so the existing integer arithmetic/compare paths handle
  time/time -> integer and time/integer -> time. `t_vhdl_physical` checks scaling, the
  >int32 range and fractional rounding. 6/8 corpus cases PASS; the other 2 advance to a
  later feature (partial aggregate, `real`).
- [x] Comparison between a vector and an integer *expression* (a signal/variable, not a
  constant) -- the `integer expression and a vector` bucket, now empty. `lowerBinary` already
  resized a *static* integer to the vector type; a dynamic one now takes the same `resizeExpr`
  the arithmetic path uses, so `unsigned ? integer` and `signed ? integer` lower to an
  unsigned / signed compare of the widened operands. `t_vhdl_intvec_cmp` checks both operand
  orders and, on identical `"1011"` bits, that `unsigned <` and `signed <` disagree.
- [x] Slices whose *width* is static but whose bounds are dynamic (e.g. `d(n*8+7 downto
  (n+1)*8 - 8)`) -- the `non-static slice width` bucket, 8 -> 4. `sliceWidth` previously matched
  only two bounds of the form `sameBase + const`; it now reduces each bound to a linear form
  `coef * atom + const` (`linearForm`, folding constants and distributing a constant multiply)
  and accepts a constant width when both bounds share one dynamic atom with the same
  coefficient, leaving the offset dynamic. `t_vhdl_dyn_slice` checks `d(n*4+3 downto n*4)` and
  the distributive lower bound. The 2 remaining cases genuinely vary the width (`mbit downto
  lbit`; an unconstrained formal's `v'length`).
- [x] `for i in obj'range generate` (and `'reverse_range`) -- the `generate range that is not
  static` bucket, now clear. The loop range parsed as a bare `'range` attribute that `staticInt`
  could not evaluate; `emitConcurrent` now reads the bounds from the prefix's type
  (`typeOfName`), as a `prefix'range` slice already does, and unrolls over the object's own
  bounds in its own direction. `t_vhdl_gen_range` covers `d'range` and a nested index over
  `q'range`. (2 cases now hit the 4096-iteration cap instead -- a legitimate limit.)
- [x] Individual record-member actuals `inp.c => cin` report `unsupported`
  (port-member binding unmodelled) instead of a parse error.
- [x] Attribute declarations in entities/architectures/packages (`attribute N : T;`,
  `attribute N of obj : T is v;`) skipped; they have no synthesis meaning.
- [x] Record/member constraints (`rec_t(x(3 downto 0), ...)`) parse and widen
  the record in place; `'range` on a signal/subtype/constant now resolves too.
- [x] Qualified-expression operands that are not aggregates: the lexer treats an
  apostrophe immediately before `(` after a name as a qualification mark
  (`std_logic'('1')`), not the character literal `'('`, so character, string and
  general-expression operands parse. `t_vhdl_qual` extended to cover them.
- [x] `type'range` / `type'reverse_range` discrete ranges on a signal, subtype or
  constant (`integer range mem_t'range`) resolve to the prefix type's bounds and
  direction via `constraintBounds`; `t_vhdl_range_attr` added.
- [x] Character and extended enumeration literals (`type e is (a, 'z', \ext\)`): the
  lexer lexes `\extended identifiers` (case-sensitive; a backslash inside is doubled),
  the enum literal list accepts a character literal, and `CHAR_LIT` resolves a
  character against the enum's literals by position; `t_vhdl_enum_lit` added.
- [x] `vunit`/PSL verification units skipped (header + balanced `{...}` body).
- [x] Concurrent and sequential aggregate assignment targets (5 + 1).
- [x] External names `<< signal ... >>` report `unsupported` at the expression.
- [x] Interface views (`view`/`of`) report `unsupported` at the declaration and port.
- [x] PSL `assert [severity X] <op> ...;` (with `{...}`/`[=N]` bodies nesting `;`)
  skipped via a brace/bracket-balanced statement end. VHDL `assert`/`report` remain.
- [x] Comma-separated `use A.all, B.all;` clauses; computed array index bounds
  (`array (size-1 downto 0)`); PSL `cover`/`restrict`/`assume` directives;
  configuration `for ... use` specs; and `signal`/`constant` interface keywords.
- [x] `process(all)` that reads through a function or type conversion (e.g.
  `registers(to_integer(unsigned(a)))`) no longer errors on `to_integer` as an
  unknown sensitivity signal; `process(all)` sensitizes only on real signals.
  Expression sensitivity items make the process combinational.
- [ ] Internal faults still open: `V3DfgSynthesize Non-ReadOnly`, `Null item passed
  to setOp3p`. These become clean Unsupported diagnostics or real fixes.

## P3 - feature gaps (corpus `UNSUPPORTED` bucket)

- [x] Static elaboration functions and packages: integer/boolean calls with
  loops and early return folded to a constant, and `math_real` real functions.
  Record-valued static functions and recursion remain.
- [x] Unconstrained array/record ports with per-instance specialization from the
  actual and port defaults (15 + 8).
- [x] Record-element port associations (`inp.c => cin`, `v.a => a`): one element of a
  record port bound to its own actual. An input record packs its element actuals with the
  existing record aggregate; an output record drives each actual from one element of a packed
  temp, mirroring how the entity reads and writes record fields. `t_vhdl_rec_port` checks an
  input and an output record port. The corpus `record-member port association` bucket is now
  empty; those 10 cases still fail for independent reasons -- the harness compiles
  `iassoc*/riassoc*` without `pkg.vhdl`, so `nat_rec` is undefined; `comp05/inter02` and
  `comp05/inter03` now clear `character'val` (below) and reach the unconstrained-record path.
- [x] Character-type attributes (`character'pos`, `'val`, `'succ`, `'pred`, `'left`/`'low`,
  `'right`/`'high`, `'length`, `'ascending`): `lowerAttr` resolved an attribute prefix only
  against declared types in `m_types`, so the predefined `character` mark fell through to
  "attribute ... of an unresolved name". It now resolves `character` (an 8-bit unsigned code)
  and lowers `'pos`/`'val` as a resize (position and code coincide) and `'succ`/`'pred` as a
  one-step code add/sub that wraps at 255; the static range attributes return 0 / 255 / 256.
  `t_vhdl_char_attr` value-checks pos/val/succ/pred and the static bounds. The corpus
  `attribute 'val`/`'pos' of an unresolved name` bucket is empty.
- [x] String and character literals in expressions: a `STRING_LIT` now packs into an unsigned
  vector of 8-bit character codes (first character most significant, via a `makeStringConst`
  shared with string generics), and a bare `CHAR_LIT` in a binary operator adopts its
  non-literal sibling's type so `inp >= 'a'` compares character codes. This required modeling
  a `string` signal as an array of 8-bit character -- it was a 1-bit-per-element vector that
  silently truncated codes -- so the signal and generic layouts now agree. `t_vhdl_lit_ctx`
  value-checks string `<`/`=` and character `>=`/`<=` against literals. Corpus: `inter01`,
  `cmp03`, `issue1036`, `issue1130` pass; the `string value in an expression` and
  `character literal ... outside a logic context` buckets are empty.
- [x] String-generic constant folding: a bound string generic was a runtime variable, so
  `if val = "one" generate` failed "generate condition that is not static". `registerString`
  now registers the fixed value as a typed constant (packed 8-bit character codes via the
  shared `makeStringConst`), so a reference folds and the condition is static; `m_strSeq` and
  the string-generic variable are gone. `t_vhdl_str_gen` binds `val => "one"` and `val => "two"`
  on two instances of one entity and checks the generate selected `o=1`/`o=0`. Corpus:
  `string01` passes; the `generate condition that is not static` bucket is empty.
- [x] Qualified package type references (`o : work.defs.rec`): `parseTypeSpec` folded a
  `work.pkg.rec` mark to just `rec` and relied on a prior `use work.pkg.all` to have imported
  it, so a qualified reference with no `use` clause failed "type 'rec' is not supported".
  `TypeSpec` now records the package qualifier, and `resolveType` imports that package on a
  type-mark miss (reusing the idempotent `importPackage`), mirroring what a qualified *value*
  reference already did. `t_vhdl_qual_pkg` value-checks a record port and signal of type
  `work.defs.rec` with no `use` clause. Corpus: `issue2826`/`issue2834` (6) pass; remaining
  record gaps are unconstrained members (`issue2119`, `comp05/inter02`/`inter03`), which stay.
- [x] Generic type parameters (`generic (type T)`), substituted from the binding.
  Generic *packages* (6) remain.
- [x] `'event` on a signal element: `clk(0)'event and clk(0)='1'` failed "attribute of a
  non-name prefix" because the prefix was an element select, not a name. A signal-element
  `X(i)'event` now lowers to a compare of that element with its previous value (the whole
  -signal level shortcut would miss a bit that did not change), registering the base signal
  as a sensitivity edge; the previous-value variables are now maintained whenever an element
  event uses one, not only under multi-sensitivity. `t_vhdl_evt` drives a 2-bit clock and
  checks that toggling the unused bit while bit 0 holds level does *not* latch. Corpus:
  `dff01/dff24`, `issue1681/repro3`, `issue1681/test_fail` pass.
- [ ] Simulation-only signal attributes (`'stable`, `'active`, `'last_value`, ...) stay
  unsupported (no synth case hits them); `X'subtype'right` chains and `alias`/subtype-range
  attribute resolution (`arr01`, `issue1280`) remain.
- [x] Real (`real`) as elaboration-time constants: `real` had no `VType` kind, so a real
  generic/constant failed "type 'real' is not supported". `real` now resolves to a new
  `VType::REAL`; a real *object* (variable/signal/port) is rejected cleanly (it has no
  runtime type, so this avoids an AstVar with no dtype crashing DFG), while a real constant
  or generic folds through `evalReal` into `m_realConsts`. A relational operator folds to a
  static bit before its operands are lowered when at least one side transitively mentions a
  real and both are `evalReal`-static, so `V < 1.5`, `integer(x) = 8`, and generate conditions
  become constants; `math_real` `sin/cos/tan/asin/acos/atan/log10` fold too.
  `t_vhdl_real` value-checks all five comparisons of a real generic plus `integer(round(V/D))`.
  Corpus: `issue1033` (x2), `issue1042`/`issue1043`/`issue1044`/`issue1075`/`issue1210` pass.
- [x] Mixed operand typing (comparison side): two comparison gaps closed. (a) A bare
  bit-string literal is typeless; `-vs >= "110"` failed "comparison between signed and
  unsigned operands" because the literal defaulted to unsigned against a signed operand.
  A bare `BITS_LIT` now adopts the real operand's signedness. (b) A matching comparison
  (`?=` / `?/=`) between an integer and a vector failed where the plain comparison already
  resizes an integer to the vector's type; the matching path now does the same.
  `t_vhdl_mix` value-checks `-signed(v) >= "110"` (incl. the 3-bit negate overflow at v=4)
  and `unsigned(v) ?= INT` / `signed(v) ?/= INT`. Corpus: `outvlg/oper01`, `issue1109` pass.
- [ ] `std_logic_signed` signedness: bare `std_logic_vector` arithmetic is modelled unsigned,
  so `slv +- bit` (`addss`/`ssubs`) and `-`/`abs` on a vector (`sunaries`) stay unsupported;
  honouring the package's signed semantics is a distinct feature.
- [x] Non-static widths from package constants: `emitPackage` registered types before
  constants (unlike entity/arch, which prescan), so a record element or array bound whose
  range named a package integer constant (`TRACE_WIDTH/2-1`, `NAHBMST-1`) failed
  "non-static array range". `emitPackage` now calls `preScanIntConstants(pkg.decls)` before
  `registerType` (and clears `m_preConsts` per package). `t_vhdl_pkg_range` (record element
  range naming a package constant) fails on the base and passes now. Corpus: `issue3184/repro4`
  passes. Remaining non-static widths: constants computed by user *functions* (`ram9`'s
  `min`/`max`, which prescan's `evalInt` cannot fold), non-static slice width, `resize`,
  and `**` exponent still report unsupported.
- [x] Conditional signal/variable assignment in a process (VHDL-2008 `v := a when c else b`,
  `x <= a when c else b`), incl. the `unaffected` waveform and a missing final `else` (target
  keeps its value). `parseSequentialStatement` parses the `when/else` chain into
  `Stmt::alternatives` (`parseWaveformValue` reads the `unaffected` keyword);
  `lowerConditionalChain` builds the nested `AstCond`, mapping `unaffected` to a read of the
  target. The constant-folder (`evalFuncBlockBody`) and `updateVarVals` bail on conditional
  assigns (null `stmt.value`). `t_vhdl_cond_assign` (ternary signal + `unaffected` + conditional
  variable) fails on the base. Corpus +7 PASS (issue2042/2043 ent, ent1, issue2045 ghdlcrash,
  issue2369 bug, synth183 test, synth40 testcase). ERROR 96->97: `issue2053/generic_fifo_fwft`
  now reaches Verilator's pre-existing unbound-generic-*type* hard error (a class already
  counted 6x), surfaced because the conditional no longer short-circuits first.
- [x] Arrays of unconstrained vectors, constrained at use: `type T is array (natural range <>)
  of std_logic_vector` left the element range open, so a use `m : T(0 to N)(W downto 0)` (index
  *and* element constrained by a paren chain) reported `unconstrained type`. `TypeSpec` now
  carries the extra index ranges (`indexConstraints`); `parseTypeSpec` reads a `t(r1)(r2)` chain
  but only after confirming each extra paren holds a real discrete range (a `name(range)` there
  is a record-member constraint and stays `unsupported`, so the record cases did not regress to
  a parse ERROR). `registerType` accepts an unconstrained vector base (element width deferred),
  and `applyIndexConstraints` clones the type per use and fills each level bottom-up, rejecting
  any residual unconstrained level. Access stays chained (`m(i)(j)`); `m(i, j)` is illegal here
  (a one-dimensional array of vectors has too many indices) and reports `unsupported` rather than
  faulting DFG. Also fixed a latent crash: `rejectRealObject` now rejects an object whose type
  stayed unconstrained (null dtype), so `signal s : T;` reports `unsupported` instead of `AstVar
  created with no dtype` (ports, which defer their width, opt out). `t_vhdl_array_of_vec`
  value-checks slice write, whole-element read, chained bit read and a three-level
  array-of-vector-of-vector. Corpus +5 PASS (eq01, issue1294/generic_array_range, issue2989/top,
  issue2989/top2, synth111/rams_sp_3d); ERROR held at 97.
- [ ] Delayed assignments: `x <= a after <t>` and `wait for <t>` (timing) still unsupported.
- [ ] Remaining aggregates: without a known target type, partial coverage (5 + 3).
- [ ] Inout/tristate limitation: `'Z'`, procedural inout assignment, the core
  `V3Tristate`/`V3DfgSynthesize` fault (same as GHDL path).
- [ ] Entity statement parts, block declarative items, impure functions, and
  remaining design unit forms.

## P4 - validation

- [x] Re-ran the native `t_vhdl_*` suite: 81 passed, 0 failed, 1 skipped.
- [x] Re-ran `nodist/ghdl_synth_suite.py --frontend native`: PASS 752,
  UNSUPPORTED 230, ERROR 97 (up from 542 / 244 / 300).
- [x] Re-ran both after N1+N2+N3: native `t_vhdl_*` 95 passed / 0 failed / 1 skipped;
  `nodist/ghdl_synth_suite.py --frontend native --suite synth` 1139 cases, PASS 842,
  UNSUPPORTED 159, ERROR 97, EXPECTED_FAIL 36, NO_TOP 5 (was 823 / 159 / 97 / 39 / 21;
  the shift is NO_TOP and EXPECTED_FAIL turning into PASS).
- [x] `VERILATOR_TEST_VHDL_FRONTEND=ghdl ./driver.py --vlt t/t_vhdl_*.py`: 72 passed,
  21 skipped, 3 failed (`t_vhdl_genfunc`, `t_vhdl_genpkg`, `t_vhdl_range_attr`); the
  same three fail with these changes stashed, so they are GHDL-path, not regressions.
- [x] `make format-c` (clang-format-21) clean; rebuilt and re-passed after.
  `make cppcheck`, `make lint-py` and `format-cmake` could not run here
- [x] Re-ran the native `t_vhdl_*` suite after N4, N5, N10, N6, N9, N19, N7, N11, N12,
  N25, N26, N21, N28, N27 and N23: 109 passed / 0 failed / 1 skipped (97 after N5, 98 after
  N10, 99 after N6, 100 after N9, 101 after N19, 102 after N7, 103 after N11, 104 after N12,
  105 after N26, 106 after N21). The GHDL path runs the same files as 80 passed / 3 failed /
  27 skipped: the same three failures, plus one skip for each test marked native-only.
  The `--suite synth` run was not repeated for N4: the machine OOM-crashed during it
  (`--jobs 4` alongside a build). Re-run it with `--jobs 2` on an idle machine before
  submitting.
- [x] Re-ran both after N16: native `t_vhdl_*` 110 passed / 0 failed / 1 skipped; the GHDL
  path 81 passed / 3 failed / 27 skipped (the same three). Corpus lints unchanged except
  fifo, 27 -> 26 errors: neotrng, spi-single-cs, spi-fpga-slave, spi-fpga-master and lfsr
  still 0, poc-divider still 6. Runs and traces unchanged: ring-buffer 4024 checks, neotrng
  37 checks and 5621-cycle trace, lfsr 1485 checks and 612-cycle trace, spi-master 48 checks
  in each of modes 0..3.

## P5 - external VHDL corpus gaps (`../vhdl_references/corpus`)

State on 2026-11-13 at `77153db`, updated after the N1+N2 name-folding, N3
specialization, N4 block, N9 pure-call fold and N19 subtype-slice fixes. Ran the nine
`vhdl_references` corpus with `--vhdl-frontend native` (wrapper appending it to
`bin/verilator`, then `make -C corpus lint|run|trace`). Nothing crashed: every gap is
a clean error
or an accepted-but-wrong name. Those harnesses were written against the GHDL XML
frontend, so they expose GHDL-parity gaps that `t_vhdl_*` does not cover. Eight of the nine
now run unmodified.

| testcase | native, unmodified | gap |
|---|---|---|
| `neotrng` | PASS 37 checks, 5621-cycle GHDL trace parity | -- |
| `spi-master` | PASS all 4 modes, 48 checks each | -- |
| `spi-fpga-master` | PASS 3520 checks | -- |
| `spi-fpga-slave` | PASS 146 checks | -- |
| `spi-single-cs` | PASS 29 checks | -- |
| `hdl-modules-fifo` | PASS 153 checks | -- |
| `hdl-modules-lfsr` | PASS 1485 checks, 612-cycle GHDL trace parity | -- |
| `hdl-modules-ring-buffer` | PASS 4024 checks | -- |
| `poc-divider` | PASS 8235 checks | -- |

No miscompile reached a corpus result: everything that elaborated passed its own
self-checking testbench, and neoTRNG matched the GHDL cycle trace exactly. The one wrong
value found so far, N25, sat in a subprogram no corpus testbench calls and surfaced while
value-checking N12; it is fixed, with `t_vhdl_formal_vec` pinning it.

- [x] N1 named port association was case-sensitive. `derivePortBounds`,
  `deriveRecordBounds` and `lowerInstance` compared `PortAssoc::formalSpelling`
  against `AstVar::name()` (both as written), so `i_rst_l => i_rst_l` against a port
  declared `i_Rst_L` reported `entity 'spi_master' has no port 'i_rst_l'`. Fixed by
  comparing the folded `PortAssoc::formal` the lexer produces
  (`V3VhdlNativeLexer.cpp:192`), as generic-map formals already did; diagnostics
  still quote the spelling.
- [x] N2 emitted C++ names kept the VHDL source case: `port (CLK : in std_logic)`
  became `VL_IN8(&CLK,0,0)` while the GHDL path emits `clk`, so every corpus
  testbench (lowercase accessors) failed on a mixed-case top. Fixed with N1:
  ports, signals, entity and specialization module names, and the identifier part of
  compiler temporaries are emitted folded; the rule is stated in the
  `V3VhdlNativeLower.cpp` header comment. Test `t_vhdl_ident_case` -- a mixed-case
  top accessed in lowercase plus a child whose design-unit, generic and port formals
  are written in another case -- fails without the fix (both frontends pass it).
- [x] N3 an entity with both generics and unconstrained ports could not be
  instantiated: `lowerInstance` called `specializeInstance` (generics) and
  `specializePortsInstance` (port ranges) as two passes, and the second reported
  `unconstrained ports on an entity with generics` -- chaining them would also have
  specialized the *specialized* module, so its cache key would have to name the
  parent specialization. Fixed by merging the two into one `specializeInstance`, which
  binds generics and port ranges in a single `emit` of a clone keyed by both
  (`<entity>|ports|...` first, then the generic part, the prefix `V3VhdlNative.cpp`
  already uses to drop the unspecialized base module). Test
  `t_vhdl_gen_unconst` -- two instances of one entity with different generic values
  and differently sized actuals -- fails without the fix; both frontends pass it.
- [x] N4 a `block` declarative part was unusable, for two unrelated reasons. An
  `attribute` declaration or specification there reported `block declarative item
  'attribute' is not supported yet`, while the entity, package, architecture and
  process declarative parts already skip such items -- and the architecture branch
  claiming so was dead code behind that skip. Separately a `constant` was invisible to
  the `subtype` declared next to it: `emitLocalDecls` (blocks and generates) and
  `emitProcessDecls` registered every type before any constant, so `subtype word_t is
  std_logic_vector(cw - 1 downto 0)` failed and cascaded into each user of the type.
  Both now `preScanIntConstants` the part first, as packages, entities and
  architectures already did, and `ScopeSave` snapshots `m_preConsts` so a pre-scan
  cannot outlive the block, generate or process that declared it. Test `t_vhdl_block`
  -- block, generate and process each naming their own constant in a subtype, plus an
  attribute spec -- fails without either half (the attribute error, or
  `unknown identifier 'cw'`/`'gw'`/`'pw'`); both frontends pass it.
- [x] N5 a generic whose constraint or default mentions an earlier generic without a
  default: `generic (depth : positive; almost_full_level : natural range 0 to depth :=
  depth)` reported `unknown identifier 'depth'` then `non-static range constraint`.
  Binding was never the problem -- `registerGenerics` walks the list in order, so the
  earlier generic is already valued. The pass that cannot elaborate the base module at
  all (no binding, no default) went on resolving the later generics' ranges and
  defaults and reported those; the caller only afterwards dropped the module, and a
  real user of such an entity already gets `has a generic without a default value, and
  no instance binds it` from `V3VhdlNative.cpp`. `registerGenerics` now stops at the
  first generic it cannot value, leaving diagnosis to the specialization path. Test
  `t_vhdl_gen_dep` -- two instances, one leaving `almost_full_level` at its default,
  which names the depth bound above it -- fails without the fix with exactly those two
  errors.
- [x] N6 a real subtype with a range: `subtype binary_real_t is real range 0.0 to
  1.0;` -> `constraint on scalar type 'real'`. `real` has no runtime representation in
  this frontend -- it survives as an elaboration-time value, and `rejectRealObject`
  refuses a real variable, signal or port -- so the range had nothing to bound and the
  guard rejected the declaration before the subtype was registered at all. `resolveType`
  now handles a constraint on `real` as it does on the other width-less scalars: the
  type stays elaboration-only, and a use that would need a runtime type is still refused
  where it appears. Test `t_vhdl_real_subtype` -- the subtype from `types_pkg.vhd`, a
  constant of that subtype, and a comparison of it folded through a package function
  returning a constrained integer subtype, both outputs value-checked -- fails without
  the fix at the subtype declaration; both frontends pass it. Blocks `hdl-modules` fifo,
  LFSR and ring-buffer (`types_pkg.vhd:56`); with it cleared fifo and ring-buffer stop at
  N16; lfsr reached N7 and then N21.
- [x] N7 a subtype mark with an index constraint on an *imported* unconstrained array
  did not resolve: `subtype taps_t is natural_vec_t(0 to 4);` where `natural_vec_t` comes
  from another package -> `type 'natural_vec_t' is not supported yet`. A package could not
  import another package at all: `lowerPackages` constructed its `Lowering` with no package
  table, so `importPackage` returned at its first guard for every package, and
  `PackageUnit` did not even keep the unit's `use` clauses -- `parsePackage` routed them
  into the *next* design unit's context list. Three small changes: keep the clauses
  (`PackageUnit::uses`, context clause plus the unit's own, like entities and
  architectures), record the inner ones with `parseUseClauseInto`, and pass the table
  under construction as the table a package may import. The array type itself was always
  published, which is why the same code inside one package worked, and an architecture
  naming the same subtype elaborated before and after. Test `t_vhdl_pkg_array_subtype` --
  a subtype of an imported unconstrained array, an array of it, that constant read back,
  and a signal declared with the subtype -- fails without the fix with that error and
  passes on both frontends. lfsr is left on 5 errors, all of them N21
  (`lfsr_pkg.vhd:147,155,156,162,166`); fifo and poc-divider were unchanged by it. Counting
  them needs an idle machine: two lint runs here were killed by the kernel (signal 9) under
  memory pressure, and the crash adds lines to the count.
- [x] N8 a bit-vector generic with a default whose range names another generic:
  `generic (len : positive; seed : std_ulogic_vector(len downto 1) := (others =>
  '1'))` -> `unknown identifier 'len'`, then `non-static array range`. Does not reproduce
  any more: re-checked after N7 on a file with that shape twice (`len` bare, and `len`
  with a range constraint), both instantiated through a generic map, elaborating clean;
  `lfsr_fibonacci_single.vhd:24-31` -- an unbound `positive` generic and a `seed` whose
  range names it -- no longer reports it either. No test added, as nothing fails to pin.
- [x] N9 an architecture constant initialized by a package function that goes through
  `math_real`: `constant w : natural := ceil_log2(8);` with
  `return natural(ceil(log2(real(value))));` in the package body reported `unknown
  identifier 'natural'` while the body was being lowered, because a real has no AST
  representation and only the pure folders know `natural(...)`, `ceil` and `log2`. Those
  folders now evaluate a call to a visible pure function whose body is a single return:
  each actual is valued in the caller's scope, bound to its formal as a literal, and the
  returned expression is folded -- `preScanIntConstants` needed nothing extra, so a type
  range may name the constant. `closedOverFormals` keeps it sound: a body naming anything
  but a formal is left unfolded, because the folders would resolve it in the caller's
  scope; innermost-only bindings and a per-declaration re-entry guard make it terminate (a
  first cut binding formals to the actual *expressions* looped forever on
  `bits_for(v) = ceil_log2(v + 1)`). Two overloads that both fit leave the call unfolded
  rather than guessed (N17). Test `t_vhdl_math_real_fn` -- `ceil_log2` as in
  `math_pkg.vhdl:205` plus a second function nesting it, feeding a constant, a subtype
  range and value-checked outputs -- fails without the fix with that error and the
  `unknown identifier` cascade; both frontends pass it. ring-buffer drops from 35 errors
  to 5 (N19); fifo still sits on N16.
- [x] N10 operator-symbol subprogram declarations did not parse:
  `function "and" (left : boolean; right : std_ulogic) return boolean;` and
  `function "/" (vec : integer_vector; int : integer) return integer_vector;` ->
  `expected an identifier, got 'and'`. A subprogram designator is the only name that may
  be an operator symbol, and the lexer already reports the quoted symbol as a string
  token, so `parseSubprogramSpec` and the subprogram `end` designator accept one;
  `takeDesignator` keeps every other designator a plain identifier, as a keyword can
  never be one. A call needs no new work: the expression parser already turns a string
  followed by an actual part into a call, so `"and"(a, b)` finds the declaration and the
  body elaborates. Test `t_vhdl_oper_subp` -- two operator functions with package
  bodies, one ended `end function "and";` and one bare `end function;`, both called in
  prefix form and value-checked over all four input pairs -- fails without the fix with
  exactly that error; both frontends pass it. Blocks PoC `utils.vhdl` (72 of them) and
  hdl-modules `types_pkg.vhd` (8); with it cleared PoC reached N11 (now N12) and
  `hdl-modules/fifo` N16.
- [x] N11 `alias` with a subprogram signature did not parse:
  `alias rmin is IEEE.math_real.realmin[real, real return real];` -> `expected ';' after
  an alias declaration`; that one parse error is all that stood between poc-divider and
  N12. The signature is now consumed (bracket-balanced) and marks the declaration as naming
  a subprogram, which changes what gets registered: not an object alias but a *synonym*
  that takes the designator's overload set, so a call of the alias resolves like a call of
  the designator -- in the same declarative part, from another package (the synonym is
  published and re-registered on import, since the alias AST node is the only thing the
  package table can carry), and in an architecture. Two ordering fixes came with it: a
  package and a subprogram body both registered their aliases before their subprograms, so
  a synonym naming a same-part function found nothing -- the parser keeps the two kinds in
  separate lists, so declaration order cannot be relied on. The signature's subtypes are
  deliberately not matched against the designator's formals: an alias may name a subtype
  where the formal names its base type, which a name comparison would wrongly reject, and
  the call site resolves the overload from the actuals anyway. That `alias x is x[...]`
  shares its name with its designator (GHDL accepts it; it denotes the designator) is why
  the synonym path refuses to append to the very list it is reading. The
  unconstrained-array alias the audit listed alongside this is not a gap: GHDL rejects
  `std_logic_vector'[integer range <>]` as well (`attribute identifier expected after '`),
  so nothing was missing there. Test `t_vhdl_alias_subp`, values confirmed by a GHDL run
  (12/14/17/5, then 5/0/10/5), marked native-only for the reason above. A subprogram's own
  declarative part needed N26 first; the test covers it as well.
- [x] N12 `'subtype` on a formal: `variable result : vec'subtype;` was a parse error
  (`expected ';' after a declaration`), which is what kept poc-divider from getting past
  `utils.vhdl:371`. A subtype indication may now be `name'subtype`: the parser records the
  object's name and `resolveType` takes its type through the helper a `name'range` bound
  already uses. That is exact where the prefix's bounds are static -- a
  `variable shifted : data'subtype;` in a process gets the signal's bounds, its loop and
  its 'length included. For a formal the mark is the formal's declared type, and the
  actual's bounds come with that binding (N25), so a `vec'subtype` variable follows the
  actual the function is called with. Test `t_vhdl_subtype_attr`, values confirmed by a
  GHDL run (72/4, then 80/4); native-only, the GHDL path refusing the type form
  (`type form 'subtype_attribute' is not supported yet`). poc-divider now reports 16
  errors: N21, N23 and N24.
- [x] N13 a predefined unconstrained array as an array element type:
  `type mat_t is array (0 to 1) of integer_vector(0 to 3);` reported
  `type 'integer_vector' is not supported yet`, while a user-declared equivalent element type
  elaborated. `registerType`'s ARRAY case now models that mark: the element is an `integer`, and
  the constraint the use gives (`integer_vector(0 to 1)`) bounds it, so the element becomes an
  array of integers with those bounds (`V3VhdlNativeLower.cpp:1430`). Test `t_vhdl_elem_vector`
  -- a 2 x 2 array of two-element integer vectors, read at both corners -- reads 63 from GHDL on
  both frontends, and was `t_vhdl_elem_vector_bad` until the fix.

- [ ] N15 `MULTITOP` for an entity that is only ever instantiated with a generic map:
  `specializeInstance` clones the entity and leaves the base module unreferenced, so
  a design whose child always binds generics reports `Multiple top level modules`
  with `Top module '<child>'` unless `--top-module` selects the top. Not
  name-related (reproduces with all-lowercase names, `f_multitop.vhd`), and the GHDL
  path does not warn; the corpus Makefiles always pass `--top-module`, so they never
  see it. The original reproducer (`/tmp/native_gaps/f_multitop.vhd`) is gone, and the two
  shapes tried here do not report it: a child always instantiated with a generic map
  elaborates clean, and two unrelated entities do report `Multiple top level modules` -- so
  the warning works, but the shape that left the specialized child as a second top is not
  recovered. Needs a fresh reproducer before it can be pinned.
  A fresh reproducer is `test_regress/t/t_vhdl_spec_top_repro.vhdl` (no `.py`, so the driver
  does not collect it): a child with a generic instantiated twice with the same binding, and
  no `--top-module`, reports `%Warning-MULTITOP: ... Top module 'child'` and `Top module
  'top'`, because `createModules` makes a base module for every entity and the instances all
  bind the generic, so the base has no cell. Attempted and reverted in the same session: the
  frontend recorded, per entity, whether an instance kept the base module (`plainUses`) and
  whether any instance specialized it (`specialized`), and `V3VhdlNative::emit` unlinked the
  base of an entity every instance specializes. That removes the warning for the reproducer
  but breaks 12 tests with `Broken link ... m_modVarp` and hard faults, so the frontend's
  bookkeeping is not a sound test for "nothing references this module": the cases that fail
  are entities that are also a design top and modules referenced from paths `lowerInstance`
  does not record. The next attempt needs the question answered on the netlist (does any
  `AstCell` name this module?) rather than on the frontend's own counters.
  Second attempt, same session, and also reverted: the gate was moved from the frontend's
  per-entity counters to the module itself -- `lowerInstance` records every module a cell is
  built for (`Design::instantiated`, one site, so no path can be missed) and `emit` unlinks
  the base of an entity that has a specialization and no cell. The reproducer lints clean,
  and the same 12 tests still fail with hard faults (`Verilator internal fault`), so the
  deletion itself is unsafe here rather than the reachability test: a module with no cell is
  not necessarily droppable, because its variables and types can still be referenced from
  elsewhere, and dropping it leaves dangling pointers that `V3Broken` reports as broken links
  or faults outright. A frontend-only fix therefore has to make the base module *not a top*
  rather than delete it, and `isTop()` is `level() == 1` set by `V3LinkCells`, which is the
  middle end. N15 stays open: the reproducer is pinned, both frontend approaches are measured
  and rejected, and the remaining one needs the middle end.

- [x] N16 a constant whose value comes from a declared function was invisible to the type
  that uses it: `constant memory_depth : positive := depth -
  to_int(enable_output_register);` and `subtype fifo_addr_t is
  u_unsigned(num_bits_needed(2 * memory_depth - 1) - 1 downto 0);` ->
  `unknown identifier 'memory_depth'`, `non-static range constraint`, then
  `type 'fifo_addr_t' is not supported yet` and a cascade (`fifo.vhd:131-141`). The cause is
  registration order, not a missing statement evaluator: a part registered all of its types
  before emitting any of its items, so the constraint was resolved while the constant behind
  it had no value. `emitConstant`'s existing lower+`foldStatic` path already evaluates
  if/else bodies and loop bodies (`/tmp/n16/p41.vhd`, `p42.vhd`), so the value was always
  computable and no pre-scan widening was needed -- only the moment was wrong. `registerTypes`
  now defers a type whose constraint names a constant of its own part that the pre-scan did
  not publish (`typeDeclNamesAny` decides from the constraint's expressions); `resolveType`
  registers a deferred type at its first use, once the emitter has published that constant;
  `finishDeferredTypes` registers what no use reached once the part's items are done. Each
  entry carries the sequence number of the part that deferred it, so a part inlined while
  another is still being emitted can neither consume nor flush the other's types, and the
  subprogram-inlining sites keep registering eagerly for the same reason. The architecture's
  types, aliases and scope snapshot also moved after the entity's items, whose constants a
  constraint may name. Test `t_vhdl_subtype_const` -- an architecture subtype sized by a
  loop-counting function, a subtype sized by an *entity* constant completed by another
  function, both read through `'length`, a truncating `resize` and a generate that is the
  subtype's first use -- fails without the fix with `unknown identifier 'bits_c'` and
  `'wide_c'`; both frontends pass it, with values from GHDL. Disabling the pieces one at a
  time brings the errors back: `/tmp/n16/p40.vhd` (fifo's shape) with the predicate or the
  first-use registration, `p46.vhd` (`type 'w_t' is not supported yet`, a subtype first used
  in a generate) with the part flush, `p47.vhd` (`unknown identifier 'wide_c'`) with the
  ordering; `p43.vhd` (a subtype written *before* its constants, illegal) reports in every
  configuration, so nothing is masked. fifo drops 27 -> 26 errors and stops at N33.

- [x] N17 an overloaded subprogram is picked by arity alone. `resolveCall` took the first
  declaration whose formal count and names accept the actuals and never compared argument
  types, so `function "and" (boolean, std_logic)` and `function "and" (std_logic, boolean)`
  both bound to the first body and a call returned the wrong value with no diagnostic. Not
  operator-specific -- two same-arity overloads of a plain name behave the same, so this
  predates operator-symbol support; the corpus declares such sets (hdl-modules
  `types_pkg.vhd` 4 x `and`, PoC `utils.vhdl` 12 operator overloads) and calls the one whose
  declaration comes second: hdl-modules `math_pkg.vhd`'s two `num_bits_needed`, whose
  `u_unsigned` body indexes its formal, so binding the integer actual to it reported
  `index or slice of a non-array value` (`math_pkg.vhd:263`).
  `resolveCall` now collects every declaration whose formal count and names accept the
  actuals and, only when that is more than one, keeps those whose formal categories accept
  the actuals' categories (`formalsAccept`): the standard's matching rule reduced to the
  category level (IEEE 1800-2023 6.5.6.2 -- an actual is compatible with a formal when the
  types are resolution-indifferent, or the actual's type is static and the two match). A
  single declaration is untouched, so no acceptance changes; when every candidate is refused
  the call reports `no declaration of 'name' accepts the actuals` instead of silently
  binding the first. Categories come from the formal's *mark* (`m_types`, then the predefined
  names) and from the actual's literal kind, `typeOfDesignator`, or the constant tables. Two
  measurements corrected the first cut: `boolean` is modelled as one LOGIC bit, so a
  `boolean` formal and a `std_ulogic` formal stay indistinguishable (fifo's
  `to_sl(enable_drop_packet)` keeps binding only because of that equality), and a constant
  whose value `evalInt` can fold has no `m_constTypes` entry, so `pick(width_c)` was
  classified unknown and fifo bound the array body to it. `pushPureCall` applies the same
  filter, which is what lets a call fold where only folding answers -- a generate range or a
  subtype constraint: with that filter alone disabled the new test reports `generate range
  that is not static`; with the `resolveCall` filter disabled it reports `index or slice of a
  non-array value` and `arithmetic on non-numeric operands`. Test `t_vhdl_overload` -- both
  pairs with the unusable declaration first, one actual a literal, a folded constant and a
  signal -- values from GHDL, native-only because the GHDL XML path answers 0 for an element
  of an array formal. fifo drops 26 -> 9 errors and stops at N34.

- [x] N18 a package's real constant was invisible outside the package: `constant half :
  binary_real_t := 0.5;` in a package spec -> `unknown identifier 'half'` wherever it was
  used. `importPackage` republished integer constants, folded AST constants, enum
  literals, types, aliases and subprograms but not `m_realConsts`, where a real constant's
  value lives. `PackageInfo` now carries `realConsts`, `emitPackage` publishes it (and
  merges it for a package body), and `importPackage` copies it -- `evalReal` uses the value
  as soon as the map is imported. A real is still elaboration-time only, so a *use* in an
  expression (rather than in a constant) keeps reporting `real value in this context`; the
  test therefore reads the constant through constant expressions. Test `t_vhdl_pkg_real` --
  `half = 0.5` as a boolean constant and `integer(half * 2.0)` -- reads 1 / 1 from GHDL on
  both frontends, and was `t_vhdl_pkg_real_bad` until the fix.

- [x] N19 a subtype mark used as a slice range did not resolve: after
  `subtype aligned_packet_address_range is natural range address_width - 1 downto
  unaligned_packet_address_width;`, `buffer_start_index <=
  buffer_start_address(aligned_packet_address_range);` reported `unknown identifier
  'aligned_packet_address_range'` at all five sites
  (`ring_buffer_write_simple.vhd:221-227`). A slice's range designator was never looked up
  as a type; only value references consult `m_types`. `lowerSel` now slices by the
  subtype's bounds and direction and `typeOfDesignator` reports the same sliced type, so
  an assignment *through* such a slice gets a right-hand side of the target's width
  (otherwise the widths disagree and `V3DfgSynthesize.cpp:545` asserts). The shared
  `discreteRangeType` answers both questions and returns null for an object name, so
  `v(i)` still indexes. Test `t_vhdl_subtype_slice` -- bounds as written and bounds naming
  a generic, read through the mark and written through it, values confirmed against a GHDL
  simulation -- fails without the fix at all four sites and passes on both frontends.
  ring-buffer now runs clean: **`PASS: hdl-modules-ring-buffer (4024 checks)`**, so the
  corpus is seven of nine.
- [x] N20 native accepted a slice whose discrete range opposes the array's direction:
  `subtype asc_range_t is natural range 4 to width - 1;` over
  `unsigned(width - 1 downto 0)` elaborated and the selected elements came out reversed,
  where GHDL stops elaboration with `slice direction doesn't match index direction` and
  the GHDL XML path with `slice direction does not match the array`. A slice's range designator
  that is a *declared subtype* now has to run with the array's direction (`lowerSel`, IEEE
  1800-2023 9.5.4); the dynamic path already refused the mismatch. The literal form is a
  different case: `t_vhdl_range` pins `d(1 to 2)` over a descending vector as a documented
  extension whose left bound is the most significant element, so that path is deliberately
  left accepting and the divergence from GHDL stands. Test `t_vhdl_slice_dir_bad` -- an
  ascending subtype over a descending vector -- reports `slice direction does not match the
  array` on both frontends, GHDL rejecting the same source; `t_vhdl_subtype_slice`,
  `t_vhdl_range` and ring-buffer's 4024 checks still pass.

- [x] N21 a function whose `return` sat inside a loop was refused:
  `get_required_lfsr_length` walks `non_zero_tap_table'range`, returns from inside a
  nested loop and ends in `assert ... severity failure` rather than in a second return, and
  lowering reported `function does not return a value on all paths` five times for it
  (`lfsr_pkg.vhd:147,155,156,162,166`) where GHDL analyzes it. The statement evaluator was
  already there -- a static loop unrolls through continuations and an `if` combines its
  arms into one expression -- but it evaluated *both* arms, so the path that runs out of
  the loop without returning, which in these bodies is the assertion, had to produce a
  value too. A condition or case selector that folds to a constant now selects the path
  taken, as a generate-if condition already did, and the path not taken is not evaluated:
  that gives a value to an `if` with a constant condition and to a `case` whose selector is
  constant, and with the latter the `others` alternative stops being demanded for an arm
  that is statically chosen. `EXIT`/`NEXT` deliberately do not take the rule: an exit
  jumps to the code after the loop, so a statically taken exit can never be the only path
  to a value.
  The folding these sites need is now the frontend's own (`foldStatic`, driven by the
  node's `numberOperate`), because `V3Const` cannot be called while lowering at all:
  `V3Global::readFiles`, which is where `V3VhdlNative::emit` runs, holds the user4 slot
  V3Const claims lazily, and a nested claim aborts (`Conflicting user use; AstUser4InUse
  request when under another AstUserInUse`, `V3Ast.h:231`), which the fifo lint hit in
  `ConstBitOpTreeVisitor::getVarInfo` the moment a condition over variables was folded. The
  three `constifyEditCpp` calls the frontend made during lowering are gone with it, so no
  VHDL lowering step claims a user slot any more, and fifo's abort is gone with them.
  Test `t_vhdl_fn_fold`: first_length returns from inside nested loops over an
  array-of-array constant and ends in an assertion, scaled takes the case arm its argument
  decides without an `others` to fall back on, dyn_max walks the same table for a value
  only known at run time; GHDL-confirmed values (3, 2, 30, 20, and dyn 2/0/3), marked
  native-only for the reason above. Without the fix the file fails on the loop body; with
  only the case rule taken back it fails on `case without 'others' in an inlined VHDL
  function body`. The nit that a refusal prints `VHDL VHDL function ...` is untouched, as
  `unsupportedError` adds the prefix the text already carries.
- [x] N22 an alias whose mark's range named an attribute of an object in the same declarative
  part did not resolve: `alias dna : unsigned(cnt'length - 1 downto 0) is cnt;` (poc's
  `xil_DNAPort.vhdl:57` has the shape) reported `attribute 'length' of an unresolved name` and
  `non-static array range`, where GHDL takes it. `registerAlias` resolved the mark as the alias
  was registered, and the architecture registered its aliases before emitting its own
  declarations, so `cnt` was not in `m_scope` yet. The architecture's alias loop now runs after
  the declarations and the deferred types (`V3VhdlNativeLower.cpp:6987`), which is also where a
  mark naming a deferred type resolves. Test `t_vhdl_alias_attr` -- an eight-bit alias over
  `cnt'length` -- reads 165 on both frontends, and was `t_vhdl_alias_attr_bad` until the fix.
- [x] N23 an attribute of a predefined integer type did not resolve, and a formal-less pure
  function named bare was never folded: `integer'high` reported `attribute 'high' of an
  unresolved name` (`/tmp/n21/p13.vhd:9`) where the same attribute of an object or of a
  declared subtype mark (`word_t'length`) resolved, so every name built on it went missing in
  turn and `integer(log2(real(integer'high)))` failed as `unknown identifier 'log2'` even in an
  architecture constant, although `log2` of a literal folds (`p11` against `p12`). Three causes,
  all three needed for poc's `get_integer_bits`: (1) `integer`/`natural`/`positive` are not
  entries in `m_types`, so neither `evalInt`'s attribute fold nor `lowerAttr` could find their
  bounds; both now consult one `predefinedIntegerType` helper, which gives them the
  `integer`-like ascending bounds GHDL reports (-2^31/0/1 up to 2^31-1), and `evalReal` gained
  the attribute case it lacked, without which `real(<attribute>)` never folded and a
  `math_real` call -- modelled only in the folders -- surfaced as `unknown identifier`. (2)
  `closedOverFormals` refused any body naming an attribute, so a pure function whose body reads
  one was never folded; it now accepts an attribute of a *type mark* only, since an object
  prefix takes its bounds from the scope rather than from the call. (3) `pushPureCall` required
  written parentheses, so `INTEGER_BITS : positive := get_integer_bits` -- the bare name of a
  function with no formal, which VHDL allows -- was not seen as a call: `evalInt`'s name case
  now tries the pure call and `pushPureCall` reads the designator from either form. Each proven
  by removing it: (1) `p13`/`p17` fail and poc returns to 16 errors, and its `lowerAttr` half
  additionally by `integer'high` read in a *run-time* assignment (`p25`), which no folder
  reaches; (2) `p17` alone fails; (3) `p17` and `p18` -- same shape, no attribute -- fail.
  `t_vhdl_type_attr` fails without the fix with the corpus symptom itself
  (`unknown identifier 'log2'`), and pins the GHDL-checked 11 / 2147483640 / -2147483648 / 0 /
  2147483647 / 0 on both frontends. poc-divider 16 errors -> 6.
- [x] N24 an aggregate as an operand of an operator had no target type:
  `signal e : signed(3 downto 0) := '0' & (0 to 2 => '0');` reported `aggregate without a
  known target type`, where GHDL takes it -- the aggregate's length is static, so its type
  can be worked out without the assignment target. `lowerExpr` now derives it when there is no
  context: the element comes from lowering the first associated value, the bounds from the
  choices (`deriveAggregateType`), and only a bit element is accepted, since a wider one would
  need the array type its element names. `others` still refuses, as the length is then unknown.
  Test `t_vhdl_aggreg_operand` -- `'1' & (0 to 2 => '0')` through an unsigned port, the same
  value through a named aggregate, and the numeric value -- reads 8 / 8 / 8 from GHDL on both
  frontends; it was `t_vhdl_aggreg_operand_bad` until the fix, and fails without the fix with
  the `aggregate without a known target type` error. poc-divider drops 5 -> 2 errors, and its
  first is now N43.
- [x] N43 an aggregate that does not cover every element of its target was refused:
  `arith/arith_Divider.vhdl:204` builds a residue as
  `(t_residue'left downto DIVIDEND_BITS => '0') & unsigned(Dividend)` and the coverage check
  refused the zeroed high part. Such an aggregate has no target type, so its own choices
  define it -- but `deriveAggregateType` read a choice range as an ascending pair, turning
  `7 downto 4` into a span of -2. A choice now covers the elements between its bounds
  whichever way it names them, so the aggregate is that many elements wide and the
  concatenation supplies the rest of the word. `t_vhdl_agg_choice` checks both directions
  against GHDL and fails without the change; poc-divider lints clean and passes its 8235
  golden checks, so no corpus design is refused by the frontend any more.
- [x] N35 a process `for` loop range has to fold, so a range the frontend cannot fold is
  refused: `for i in 1 to pick(n) - 100 loop` over a port `n` reported `loop range that is
  not static`, though a sequential loop evaluates its bounds when it is reached (poc's
  `arith_Divider.vhdl:206` is this shape). The unroller kept the index as a compile-time
  constant, so the range had to be one. Such a loop now lowers to a runtime `AstLoop` over
  a fresh integer variable that the body reads and writes through the scope, with the
  bounds and the step lowered as expressions; a body with `exit` or `next` stays refused,
  since they act on an iteration the runtime loop cannot see. Test `t_vhdl_loop_range`:
  `n=4` sums to 10, and `n=1` then adds 1 to the previous run's 10, which is what GHDL
  does with a process variable that keeps its value between runs. It was
  `t_vhdl_loop_range_bad` until the fix. poc-divider drops 2 -> 1 error, leaving N43.
- [x] N25 an element of an unconstrained array formal read as zero, with no diagnostic:
  `function e(vec : vec_t; index : integer) return integer is variable copy :
  vec_t(0 to 3); begin copy(index) := vec(index); return copy(index); end;` called as
  `e(data, 1)` over `data = (10, 20, 30, 40)` returned 0 where GHDL returns 20, both with
  the explicit constraint and with `vec'subtype` as the variable's subtype, while
  arithmetic on such an element was refused outright (`arithmetic on non-numeric
  operands`) -- which is why no corpus result showed it: poc's vector operators are never
  called. The formal's binding took its type from the lowered actual's data type, which
  has already flattened the array: index bounds gone and `elemWidth` 1, so `vec(index)`
  became a bit select of the packed actual and `lowerSel` had nothing to complain about.
  It now takes the actual's VHDL type first, falling back to the data type only where that
  is unknown -- the same rule the two other binding sites, a subprogram's alias and its
  locals, already followed. Element width and index bounds arrive with it, so `vec'range`,
  `vec'length` and a `vec'subtype` variable follow the actual a call passes, in either
  direction. Test `t_vhdl_formal_vec` covers both directions and all four uses; without
  the fix it fails on the first of them.
- [x] N26 a subprogram alias declared in a subprogram's own declarative part did not
  resolve: `function twice(value : integer) return integer is alias add2 is
  add[integer, integer return integer]; begin return add2(value, value);` reported `call to
  subprogram 'add' whose body was not read is not supported yet` (at the designator's
  line), where GHDL analyzes it and the identical alias in a package header works.
  Inlining binds a subprogram's aliases itself -- its mark gives an alias bounds of its
  own -- so N11's synonym rule never ran there and the designator was valued as an object
  alias is: an unqualified name then reaches the call paths as the body-less prototype from
  the package header, a qualified one reports `package 'p' has no declaration 'add'`, and
  an alias naming a subprogram of the same declarative part reports that its formals got no
  actuals. Such an alias is now registered as a synonym there too, by the one rule N11
  introduced. `t_vhdl_alias_subp` gained both shapes -- an alias naming a package function
  and one naming a sibling subprogram -- with GHDL-confirmed values (i_a = 7: inside 16,
  nested 10; i_a = 0: 2 and 3); without the fix it fails on each of them.
- [x] N27 a name inside a folded function body did not resolve when the call came from
  another unit's static expression: `for row in table'range` in a package function reported
  `function loop range of an unresolved name`, where the identical body called from an
  architecture constant folded (`/tmp/n21/p4.vhd`, twenty lines: an entity with generic
  default `len : integer := first_length(5, 2)` plus an instantiation, which then also
  reported `unknown identifier 'len'`). The body of an inlined subprogram evaluates against
  `m_moduleScope`, the snapshot of the calling unit that `captureBaseFor` hands it, and that
  snapshot was taken partway through a module -- after its generics and types -- so the
  three elaboration steps that can call a function earlier ran against whatever a previous
  unit had left behind: a package constant's value (`emitPackage` took no snapshot at all),
  an entity generic default, and a generic's declared type, whose range `lowerInstance`
  resolves against the instance's bindings. Each now takes the snapshot immediately before
  it, the rule the module's declarative items and concurrent statements already followed.
  Test `t_vhdl_fold_scope` folds `first_over` from a package body constant, from a generic
  default and from an architecture, and reads a generic whose type range calls the
  function; GHDL-confirmed values (2 for the constant and the default, 2 and 4 for the
  architecture, seed length 4 for the type range). With the package snapshot removed the
  file fails on the package constant, with the module one on the generic default. The
  third site resists a small test: the snapshot a previous unit left in the same `Lowering`
  happens to carry the names once one package supplies them all, and neither an unbound
  generic (`/tmp/n21/p9.vhd`) nor an unconstrained port (`p10.vhd`) changes that -- it is
  pinned by the corpus instead, where lfsr's `lfsr_pkg.vhd:152` needs it. All three
  together take hdl-modules-lfsr from 15 errors to a clean lint, `PASS (1485 checks)` and
  612-cycle GHDL trace parity. `utils.vhdl:753`'s `function loop range that is not static`
  looks alike but is N23's cascade, through `INTEGER_BITS : positive := get_integer_bits`.
- [x] N28 a package body did not see what its specification declares: naming a constant of
  the specification from a constant declaration in the body reported `unknown identifier`,
  and naming it through a body function reported `function loop range of an unresolved
  name` (`/tmp/n21/p7.vhd`, `p8.vhd`). A body is nested in its specification, so what the
  specification declares, and what its own `use` clauses made visible to it, are visible
  in the body; `emitPackage` cleared the scope for every unit and imported only the
  packages the unit's `use` clauses name, so a body got neither. It now imports its own
  specification, which is always published first -- `lowerPackages` runs a pass over the
  specifications before any body, and an instance publishes its specification directly
  before its body. `reserveName` already lets a local declaration shadow an imported name,
  which is how a body completes a deferred constant, so that path needed nothing.
  Test `t_vhdl_pkg_body_scope`: body constants read the specification's, two deferred
  constants are completed in the body, and a body function loops over a specification
  constant; GHDL-confirmed values (mask 1, width 6, span 24, rotate-and-mask 1, 0, 1, 1),
  and it passes on the GHDL path too. Without the import it fails on the body's first
  constant. poc-divider's `utils.vhdl:344` body is this shape over its specification and
  over `ieee.math_real`, yet its count is unchanged, because its first error is N23.
- [x] N29 a package specification constant initialized by a function whose body is in the
  package body was refused: `constant picked : integer := first_length(5, 2);` in the
  specification reports `call to subprogram 'first_length' whose body was not read`, where
  GHDL accepts it with no more than a `-Welaboration` warning (`/tmp/n21/p6.vhd:17`). The
  specification is elaborated in a pass of its own, so the body's definition is not
  published yet when the constant is folded. Fixed in the frontend: `emitConstant` records
  such a constant on `Lowering::m_deferredConsts` when the initializer names a visible
  subprogram with no body (`deferredOk`, i.e. a specification) instead of reporting the
  call, and `emitPackage` retries the recorded constants once the body's subprograms are
  registered, so the value lands in the package's `consts` and importers see it. A name
  with arguments parses as an `INDEX` until the prefix is known to name a subprogram, so
  the deferral consults the prefix's name as well as a `CALL`'s folded name. Test
  `t_vhdl_pkg_spec_fn`: the first constant folds to 7, and a second one that calls the
  same function with the first as an actual folds to 10, which also pins that a deferred
  constant is published before the next one is completed; GHDL reports the same values.
  Without the deferral the test reports the old `whose body was not read` error. Blocks no
  corpus design: poc completes such a constant in the body, which N28 covers.
- [x] N30 `real` is modelled as a `double`, and IEEE 1800 leaves its precision
  implementation-defined, so a conversion GHDL rounds gives another value: poc's
  `integer(log2(real(integer'high)))` is 31 there (its `real` holds 2147483647 as 2^31) and 30
  here, so `INTEGER_BITS` is 32 versus 31. Both are conforming -- the standard fixes no
  precision -- so no code change is right; `t_vhdl_type_attr` keeps its `log2` argument a
  power of two so both frontends agree, and this stays a documented divergence.
- [x] N31 an attribute of a type mark *with* an argument folded statically but was refused in
  a value path: `natural'pos(3)` in an output assignment reported `attribute 'pos' with
  arguments is not supported yet`, while the same expression in a constant folded.
  `lowerAttr` now asks `evalInt` for the answer before refusing, so the value path and the
  folder agree (`pos` of an integer value is that value). Test `t_vhdl_attr_arg` -- `pos` of a
  literal and of a constant expression, in assignments -- reads 3 / 5 from GHDL on both
  frontends, and was `t_vhdl_attr_arg_bad` until the fix.
- [x] N32 a pure call whose actual is not static folded forever: a constant whose initializer
  read a signal (`constant d_c : integer := width_of(to_integer(to_unsigned(ctrl, 8)));`,
  `width_of` looping over `2 ** i`) was measured allocating 87 GB in 65 s and then hitting the
  OOM killer. The recorded shape does not reproduce now: under a 2 GB `ulimit` it reports
  `this statement form in an inlined VHDL function body is not supported yet` and exits, because
  the statement evaluator refuses a form it cannot evaluate (`evalFuncBlockBody`) instead of
  running it, and both loop entry points cap the unroll at 4096 iterations (`lowerFor:5039`,
  `evalFuncFor:6024`). Test `t_vhdl_fold_nostatic_bad` pins the refusal, so a later change that
  lets the evaluator run such a body again fails the suite.

- [x] N36 a `component` declaration and a `component` instantiation were refused, which
  stopped most of the cores outside the corpus. A sweep of `../vhdl_references/third_party`,
  each core's design files linted with its own top (`/tmp/vlf/sweep2.py`, logs
  `/tmp/vlf/logs/sweep2_*.log`; errors counted up to the frontend's exit, blockers in source
  order). Cores the change unblocked, before and after:

  | core | files | before | after | first blocker now |
  |---|---|---|---|---|
  | neo430 | 33 | 1 | 50* | `impure function call 'init_boot_rom'` |
  | neorv32 | 61 | 2 | 1 | `process declarative item 'file'` (N39) |
  | patmos | 32 | 2 | 1 | `wait for a delay` (N37) |
  | t80 | 13 | 1 | 2 | `case without 'others' in an inlined function body` (N38 pinned), then a logical mix of operand types, `t80_reg` port mismatch, `concurrent assignment to an inout port`, `to_x01` unresolved; no fault (re-measured, 24 refusals) |
  | vhdl-lib | 32 | 29 | 2 | `subprogram declarative item 'file'` (N39) |
  | f32c | 430 | 42 | 27 | `configuration` (N40), plus a procedure-call parse error |

  (*) the error limit: neo430 now reaches `non-static slice width` 24 times and an
  undeclared external `SP256K` component, where it used to stop at the package. The other
  six cores were not re-run because `component` was never their first blocker: microwatt
  stops at `expected 'then'` (`bitsort.vhdl:80`), bit-serial at `after` (N37), fpga_puf at
  `non-static slice width`, potato at `aggregate without a known target type`, and t65 and
  rpu at cascades of a refused component binding.
  A component declaration is now accepted in a package, entity and block declarative part as
  well as an architecture's (`parseComponentDecl`), and an instantiation may name its entity
  with the `component` keyword (`label : component NAME port map (...)`). The declaration is
  dropped rather than modelled, because a named instantiation binds to the entity of that
  name and the interface list only shapes the association check this frontend does not
  model -- the same thing the architecture path already did, and the one field the lowering
  ever read (`Architecture::components`) was dead. Test `t_vhdl_component` -- the component
  declared in a package and instantiated by name, the same entity through the `component`
  keyword with its generic defaulted, and an entity instantiation as the control, each adding
  three and five -- reads 8 on all three outputs, with GHDL, on both frontends; it fails
  without the fix at `t_vhdl_component.vhdl:13` with `package declarative item 'component'
  is not supported yet`. fifo (9) and poc (5) errors are unchanged and every corpus case
  still passes.
- [x] N38 t80 no longer faults the frontend: `T80_MCode.vhd:168,179` still report
  `case without 'others' in an inlined VHDL function body`, but the
  `Internal Error: ../V3Ast.cpp:491: Null item passed to setOp1p` recorded at
  `T80_ALU.vhd:115` (`/tmp/vlf/logs/sweep2_t80.log`) is gone -- the whole core now reports 24
  clean refusals and no fault or assertion. Nothing targeted removed it, so the shape is
  pinned rather than assumed: `t_vhdl_fn_case_bad` reproduces the refusal through a pure call
  whose actual comes from a signal, which is what leaves `evalFuncCase` without a constant
  selector to pick an arm with, and its golden would show an internal error instead of the
  refusal if the null operand came back. GHDL analyzes the same source, so the test is
  native-only.
- [x] N39 a `file` declaration was refused wherever it appeared: `process declarative item
  'file'` (neorv32 `neorv32_cpu_trace.vhd:788`), `subprogram declarative item 'file'`
  (vhdl-lib `PatternGen.vhd:99`, f32c `rtl/soc/rom.vhd:82`). `skipFileDecl()` now consumes
  one in every declarative part that may carry it -- entity, subprogram, architecture, block,
  process and package -- and it is skipped rather than modelled: a file has no synthesis
  meaning, and the declaration's type is never resolved, which is why `file f : text;` works
  without `std.textio` being lowered (GHDL requires the type to be visible, so the test uses
  it). Test `t_vhdl_file_decl` -- the declaration in all six parts, with a value driven past
  them -- reads 5 and '1' from GHDL on both frontends, and fails without the fix at
  `t_vhdl_file_decl.vhdl:13` with `package declarative item 'file' is not supported yet`.
- [x] N40 a `configuration` design unit was refused (f32c
  `rtl/lattice/chip/xo2/ddr_out.vhd:87`, with `group` and `entity statement part` beside it),
  which was f32c's first blocker after N36. It is now parsed into a `ConfigurationUnit` on
  the design library and checked against the architecture it configures: a binding that
  names an entity the architecture already instantiates directly changes nothing and the
  design elaborates; a binding that names a component (`component instantiation is not
  supported yet`), an instance the architecture does not instantiate, or an entity other
  than the instantiated one is refused with a diagnostic. An instantiation now records
  whether it names an entity or a component, which is what makes the check possible. Test
  `t_vhdl_config` reads the configured buffer's value from GHDL-verified expectations, and
  `t_vhdl_config_comp_bad` pins the component refusal; the former
  `t_vhdl_config_bad` is gone.
- [x] N37 a VHDL testbench can neither be elaborated nor self-checked, so every corpus case
  has to drive its design from C++. Measured: `after` is refused at parse
  (`V3VhdlNativeParse.cpp:2022`, `:2041`, `:2430`) and `wait for` at `:2338`; a sequential
  `report` or `assert` parses as `Stmt::REPORT` (`V3VhdlNativeParse.cpp:2348-2366`) and a
  concurrent `assert` as `Concurrent::ASSERT` (`:1984`), and lowering drops both
  (`V3VhdlNativeLower.cpp:4539`, `:7702`), so nothing a testbench reports or checks reaches
  the model. `--timing` is already accepted in VHDL mode -- `/tmp/n17tb/tb_dff.vhd` fails on
  `after` and on nothing else -- and the primitive such a testbench needs is generic and
  present: `AstDelay` (`V3AstNodeStmt.h:652`) is lowered by `V3Timing` into a `CAwait` on the
  global delay scheduler, and time literals already parse (`V3VhdlNativeParse.cpp:417-428`).
  Of the 262 testbench files under `third_party`, 188 use `after`, 148 use `wait for` and none
  uses `std.env`, so `after`/`wait for` plus a `finish` equivalent is what unlocks them.
  Attempted end to end and reverted, because the lowering segfaults rather than reporting.
  What was measured: `after` in a concurrent assignment is refused at
  `V3VhdlNativeParse.cpp:2068`, a variable assignment at `:2434`, a signal assignment at
  `:2458`, and `wait for` at `:2360`; `Stmt::WAIT` has no delay field, `Stmt` and
  `Concurrent` have no `after` field, and `lowerWaitProcess` (`V3VhdlNativeLower.cpp:4900`)
  splits a process at its first wait into an `AstInitialStatic` prefix plus a
  sensitivity-list process, which cannot express a timed repeat. A draft that added
  `Stmt::delay`/`Concurrent::delay`, parsed all four forms, lowered `report` to `AstDisplay`,
  `std.env.finish` to `AstFinish`, and a delay-wait to `AstLoop`+`AstLoopTest`+`AstDelay`
  under an `AstInitial` built cleanly but SIGSEGV'd in the frontend on the smallest case
  (`process begin wait for 5 ns; end process;`), both with `--lint-only` and with
  `--binary`, and it does not reproduce under `gdb`, so the fault is not yet localized.
  Next: get a real stack (gdb with `VERILATOR_ROOT` set and the fault captured to a file)
  or bisect by lowering the timed loop through `AstDelay` alone, without `AstLoop`. The
  pieces themselves are all present in the middle end: `AstDelay`
  (`V3AstNodeStmt.h:652`, with `timeunit()`), `AstInitial`, `AstDisplay`,
  `AstFinish`, and `m_modp->timeunit()` for the scale, which is `NONE` when no
  `--timescale` is given and must default to 1 ps.
  Landed separately as `b80509c`: a `report` with a literal message lowers to `AstDisplay`,
  and `std.env.finish` to `AstFinish`, so a testbench can state what it observed and end its
  own run; a computed message is still dropped (`lowerReport`), because only a literal is
  modelled. `t_vhdl_tb` runs under `--binary`, checks a signal through an `if`, prints what
  it saw and finishes, and GHDL prints the same two lines. Still open: the delays, which is
  where the segfault is.
  The delay half then landed as `7e3fe94`: `wait for <t>` parses into `Stmt::delay`, and a
  process whose wait is a delay lowers to `AstInitial{ pre, AstDelay, post }` and then
  suspends, with the literal scaled to `m_modp->timeunit()` (1 ps when the design has no
  timescale). `t_vhdl_tb` now waits 5 ns, checks a signal and finishes, and GHDL prints the
  same two lines. The crash was in `AstLoop` + `AstLoopTest` inside the initial, not in
  `AstDelay` or `AstInitial`: the same delay without the loop lints and runs clean. Still
  open: a process that repeats after a delay (a clock generator) and the concurrent `after`.
  The crash is found and fixed in `e162c1e`: it was `AstNode::addNext` on an empty statement
  list -- a process whose wait is its first statement (the clock generator's checker) has no
  prefix to link the delay onto -- and not `AstLoop` or `AstDelay` at all. A process whose
  wait is a delay now repeats: its body lowers once under an `AstLoop` and the delay sits
  inside it, which is the clock generator (`clk <= not clk; wait for 5 ns;`). `t_vhdl_tb`
  generates that clock, checks a toggle flip-flop at 22 ns and finishes; GHDL reports the
  same value at the same time. Still open: the concurrent `after` form, which needs a timed
  `always` rather than a process.
  Closed by `50c21da`: the concurrent `after` parses into `Concurrent::delay` and lowers to
  `{ AstDelay, assign }` under a timed `AstAlways`, the same `lowerDelay` a process wait uses,
  so both forms share the time unit. `t_vhdl_tb_conc` checks the clock at 22 ns and the
  superseded `t_vhdl_tb_wait_bad` is gone. Both testbenches now agree across three
  simulators: native, GHDL and nvc (`nvc --std=2008`, installed at `~/.local/bin/nvc`) report
  the same value at the same time, so nvc is the second opinion to prefer when GHDL and the
  native frontend disagree.
- [ ] N41 a procedure with `signal` formals faults the frontend when a concurrent call inlines
  it: the body's variable assignment becomes a write outside a process or function
  (`Internal Error: ../V3Delayed.cpp:577: Write not in a process or function`). Reached from
  t80's `AddSub` (`/tmp/n38/p8.vhd`), whose procedure takes `A`/`B` as unconstrained
  `std_logic_vector` formals and `Res`/`Carry` as `signal ... out`, and is called from the
  architecture's statement part. `/tmp/n38/p15.vhd` (the same shape without the if) is clean
  only because each formal then has exactly one assignment; the real trigger is any write
  the inlining cannot express as a continuous assignment. `t_vhdl_proc_var_write_bad` pins
  it; the measured root cause and the shape that fixes it are in *N41 measured* below.
- [x] N42 two processes that each declare a variable of the same name collided in the mangled
  scope: `Duplicate declaration of VARSCOPE 't.vhdlvar_1__r'`, where each process should get
  its own variable.
  `t_vhdl_proc_var_dup_bad` pinned it; `t_vhdl_proc_var_dup` now checks both values, which
  GHDL confirms (`vars are 41 42`). The mangling counter was local to one declarative part,
  so every process restarted at 1; it is a `Lowering` member now.

Every open finding above has a reproducer in `test_regress/t/`. An expected-error test
(`t_vhdl_<slug>_bad`, native-only because GHDL accepts the source, with a `.native.out`
golden) pins the refused ones; a `.vhdl`-only reproducer covers the three that cannot be a
suite case -- one over-accepts, one differs only in value, one exhausts memory:

  | finding | reproducer |
  |---|---|
  | N13 | `t_vhdl_elem_vector` (value test now; `..._bad` until the fix) |
  | N15 | `t_vhdl_spec_top_repro.vhdl` (no `.py`; a fix attempt was reverted, see the finding) |
  | N18 | `t_vhdl_pkg_real` (value test now; `..._bad` until the fix) |
  | N20 | `t_vhdl_slice_dir_bad` (expected-error test now that the check exists) |
  | N22 | `t_vhdl_alias_attr` (value test now; `..._bad` until the fix) |
  | N24 | `t_vhdl_aggreg_operand` (value test now; `..._bad` until the fix) |
  | N43 | `t_vhdl_agg_choice` (value test, added with the fix) |
  | N29 | `t_vhdl_pkg_spec_fn` (value test; was `t_vhdl_pkg_spec_fn_bad` before the fix) |
  | N30 | `t_vhdl_real_bits_repro.vhdl` (value difference only) |
  | N31 | `t_vhdl_attr_arg` (value test now; `..._bad` until the fix) |
  | N32 | `t_vhdl_fold_nostatic_bad` (expected-error test; the shape no longer allocates) |
  | N35 | `t_vhdl_loop_range` (value test now; `..._bad` until the fix) |
  | N37 | `t_vhdl_tb_wait_bad` |
  | N38 | `t_vhdl_fn_case_bad` (expected-error; the fault itself no longer reproduces) |
  | N40 | `t_vhdl_config` (value test) and `t_vhdl_config_comp_bad` (expected-error) |
  | N41 | `t_vhdl_proc_var_write_bad` |
  | N42 | `t_vhdl_proc_var_dup` (value test; was `t_vhdl_proc_var_dup_bad` before the fix) |

  Each closes with the fix: the test becomes a value check, as the ones added with this round
  of fixes already are (`t_vhdl_component`, `t_vhdl_file_decl`, `t_vhdl_defer_type`,
  `t_vhdl_proc_else`).

Native ahead of the GHDL path on this corpus (keep, no action): `--top-module` matches
case-insensitively (the GHDL path needs the lowercased name, and folded emitted names
now agree with it); a static package function over an array-of-array constant folds
(`for l in table'range`, `table(i)(j)` -- the GHDL path reports `non-static range
limit`); and for a package function called from an architecture constant the GHDL
path faults (`V3DfgSynthesize.cpp:545: Mismatched width reached DFG`) where native
reports an error. A generic whose range or default names another generic elaborates
only natively: the GHDL XML path reports `Generic 'almost_full_level' has no static
default and is not mapped`, so `t_vhdl_gen_dep` is marked native-only. The GHDL XML path
has no subprogram alias at all -- every call through one is `function call 'add5' is not
supported yet` -- so `t_vhdl_alias_subp` is marked native-only as well. The GHDL XML path
also indexes only vectors: it refuses an element of an array formal, an element assignment
in an inlined function, and a function whose body loops, so `t_vhdl_formal_vec` is
native-only too. The GHDL XML path reports that same refusal for a return from inside a
loop, so `t_vhdl_fn_fold` and `t_vhdl_fold_scope` are native-only as well.
  `t_vhdl_overload` is native-only for the same reason: the GHDL XML path answers 0 for an
  element of an array formal, so a pair distinguishable only by an array formal cannot be
  pinned through it.

Reproducers for each item (1-14 line VHDL files, GHDL-path control included) are in
`/tmp/native_gaps/` (`r1`..`r14`, `c_oper.vhd` for N10, `d2/d3/d4` for N4,
`f_multitop.vhd` for N15, `a1/a3/e1` for N1/N14); they are scratch files, not
regression tests -- the fixed N1+N2 case is promoted to
`test_regress/t/t_vhdl_ident_case`, N3 to `test_regress/t/t_vhdl_gen_unconst`, N4 to
`test_regress/t/t_vhdl_block`, N5 to `test_regress/t/t_vhdl_gen_dep`, N10 to
`test_regress/t/t_vhdl_oper_subp`, N6 to `test_regress/t/t_vhdl_real_subtype`, N9 to
`test_regress/t/t_vhdl_math_real_fn` (distinct from the repo's `t_vhdl_math_real`, which
covers the same round trip written inline rather than through a function), N19 to
`test_regress/t/t_vhdl_subtype_slice`, N7 to `test_regress/t/t_vhdl_pkg_array_subtype`,
N11 to `test_regress/t/t_vhdl_alias_subp` (N26 extended that file rather than adding one),
N12 to `test_regress/t/t_vhdl_subtype_attr`, N25 to `test_regress/t/t_vhdl_formal_vec`, N21
to `test_regress/t/t_vhdl_fn_fold`, N28 to `test_regress/t/t_vhdl_pkg_body_scope`, N27 to
`test_regress/t/t_vhdl_fold_scope` and N23 to `test_regress/t/t_vhdl_type_attr`; N16
  `/tmp/n16/p40,p46,p47` promote to `test_regress/t/t_vhdl_subtype_const` and N17 to
  `test_regress/t/t_vhdl_overload` and N34 to `test_regress/t/t_vhdl_defer_type`. The
  twenty-two new test trios accompany the fixes and are committed (`bf72849`, `a42be89`,
  `23d9990`, `4db7a87`, `2dbc12e`, `63cdd33`, `b80509c`, `7e3fe94`, `e162c1e`, `50c21da`,
  `7a292ab`); this tracker stays untracked.

## P6 - corpus sweep 2 (2026-11-14)

Re-linted `third_party` with the native frontend after N37 and N40 closed. Both
neorv32 and neo430 now get past their first error and expose deeper ones, and
t80 exposes its own. Counts are `%Error` lines over the whole core.

Closed by this sweep:

- [x] a concurrent waveform list (`sig <= '0', '1' after 5 ns;`) was refused at
  parse; it is parsed into `Concurrent::waveform` and lowered to a timed always.
  `t_vhdl_waveform` samples it at 2 ns and 7 ns, GHDL agrees. Commit `7909341`.
- [x] the character literal for a single quote (`'''`) reached the parser as a
  stray mark; the lexer now accepts it. `t_vhdl_char_quote`. Commit `1a947ad`.

Found by this sweep:

- [x] a predefined scalar type mark applied to a value is not recognised as a
  conversion, so `boolean(x)` and `std_logic(x)` reported `unknown identifier`.
  Closed in `fe23325`: an identity conversion for `boolean`, `bit`, `std_logic`
  and `std_ulogic` in the `Expr::INDEX` case of `lowerExpr`, which is what the
  two-state model makes of a resolved-to-unresolved or boolean-to-boolean
  conversion. `t_vhdl_scalar_conv` checks a boolean and a std_logic conversion
  against GHDL. Measured: neorv32 9 `boolean` errors
  (`rtl/core/neorv32_top.vhd:313`) and neo430 2 `std_logic` errors
  (`rtl/fpga_specific/lattice_ice40up/neo430_imem.ice40up_spram.vhd:180`) are gone.
- [x] `unknown identifier 'firq'`, neorv32, 14 occurrences. Root cause, found by following
  the type rather than the name: `type firq_t is array (firq_enum_t) of std_ulogic` --
  an unconstrained array whose index is an enumeration. `registerType` refused any index
  without an explicit range, so `firq_t` never entered `m_types`; `signal firq : firq_t`
  then failed to resolve its type (`type 'firq_t' is not supported yet` at
  `rtl/core/neorv32_top.vhd:385`) and the signal was never declared, so all 14 uses
  reported the name as unknown. Fixed in `eb9849c`: an index that is a type mark takes the
  mark's positions, and an enum literal used as an index converts to its position (the
  second half was hidden behind the first -- it cost 16 `non-integer index` errors once the
  type registered, because an enum literal's dtype is an `AstEnumDType`, which
  `typeOfDType` reports as a vector). `t_vhdl_enum_index` reads two elements of such an
  array and GHDL agrees. neorv32 is down to 11 `empty or reversed slice`, 7 inout-port
  writes, 1 unknown identifier and 3 entity port errors.
- [ ] `empty or reversed slice`, neorv32, 11 occurrences.
- [ ] `procedural assignment to an inout port`, neorv32, 7 occurrences
  (`sim/psram_model.vhd`).
- [ ] `array type without an index constraint`, neorv32, 3 occurrences
  (`rtl/core/neorv32_top.vhd`).
- [ ] `impure function call 'init_imem'`, neo430, 4 occurrences
  (`rtl/fpga_specific/lattice_ice40up/neo430_imem.ice40up_spram.vhd`).
- [ ] entity port errors: `neorv32_sys_reset` has no port `xrstn_wdt_o`;
  `neo430_wb_interface` has no port `wren_i`/`wb_we_o`/`wb_sel_o`/`wb_dat_o`;
  `t80_reg` has no port `Clk`/`CEN`/`WEH`/`WEL`/`AddrA` -- in t80's case the
  instantiation is a default binding (`Regs : T80_Reg`) with no component
  declaration, so the port list the check uses is not the entity's.
- [ ] `case without 'others'` in an inlined function body, t80, 2 occurrences
  (`T80_MCode.vhd:179`).
- [ ] `logical operation on operands of different types`, t80, 1 occurrence
  (`T80.vhd:693`).

## P7 - neorv32 after the firq fix (2026-11-14)

neorv32's errors after `eb9849c`, with the cause of each cluster.

- [ ] a null slice is refused: `type vec is array (n-1 downto 0)` indexed with a
  range that is empty for the current generic (`addr(awidth_c+1 downto 2)` with
  `MEM_SIZE = 4` gives `awidth_c = 0`, so the range is `1 downto 2`). VHDL makes
  that an empty vector whose value is zero, and the design relies on it. 11
  occurrences in `sim/xbus_fmem.vhd`. Two attempts, both reverted: a plain
  zero-width `AstConst` trades the refusal for `conversion of a non-vector value`
  at every use (`unsigned(null)`), and a zero-width vector const aborts the
  frontend. The fix needs a real zero-width vector value, not a scalar const.
- [ ] `procedural assignment to an inout port`, 7 occurrences in
  `sim/psram_model.vhd:134,214,222-225,234`.
- [ ] `recursive call of function 'sel_suv_f' is not supported yet`,
  `rtl/core/neorv32_top.vhd:317` -- the function's own body recurses, so the pure
  folder stops.
- [ ] `unknown identifier 'cpu_boot_addr_c'`, `rtl/core/neorv32_top.vhd:554` --
  the same shape as `firq` was: a name whose declaration failed.
- [ ] entity port errors in `neorv32_top.vhd:502,515,725,746` and
  `neorv32_smc.vhd:389`: `neorv32_sys_reset`, `neorv32_sys_clock`,
  `neorv32_bus_amo_rmw`, `neorv32_bus_amo_rvs`, `neorv32_smc_mac` have no port
  `clk_i` and friends. **These entities are not in the checkout** --
  `find neorv32 -name '*sys_reset*'` is empty -- so this is a partial tree, not a
  frontend gap; GHDL would fail the same way.

## P8 - sim versus synthesizable (2026-11-14)

Classified every remaining error by the file it is in. **The remaining gaps are
overwhelmingly in synthesizable RTL**; the sim-only ones are the two neorv32
findings from P7 (null slice in `sim/xbus_fmem.vhd`, inout write in
`sim/psram_model.vhd`) and one neo430 testbench file.

=============================  ===========  =======  ==========================================
core                           sim errors   RTL      dominant RTL family
=============================  ===========  =======  ==========================================
neorv32                        18           32+      non-static slice width (15) and
                                                        attribute of a non-name prefix (9), both
                                                        in `rtl/core/neorv32_cpu_alu_fpu.vhd`
neo430                         1            49       non-static slice width (~9, in uart, twi,
                                                        wb_interface, sysconfig), impure function
                                                        call (4, ice40up memory init), concurrent
                                                        assignment to an inout port (2, twi)
t80                            0            24       default-binding port check (t80_reg), case
                                                        without others in an inlined function (2),
                                                        unknown identifier (2)
=============================  ===========  =======  ==========================================

The dominant RTL family is one shape: a slice whose bounds come from an attribute
of a *record element* or another non-name prefix --
`rx.sreg(rx.sreg'left downto 1)`, `serial.sreg(serial.sreg'left-1 downto 0)` --
so the frontend cannot get the prefix's type statically and refuses the width.
That one family accounts for 15 errors in neorv32's FPU and 9 in the FPU's
`attribute of a non-name prefix` form, plus neo430's UART, TWI and wishbone
interface. It is the next target.

### Slice bounds from the prefix's own attribute

Partly closed in `273a50f`: a bound that is the sliced prefix's own `'left` or
`'right` now comes from the prefix's type, which the slice already has, so
`s(s'left downto 1)` works and `t_vhdl_attr_slice` checks it against GHDL.
neorv32's FPU drops from 15 to 8 non-static slice errors.

What remains in that family:

- compound bounds such as `s(s'left-1 downto 0)` -- the fold handles a bare
  attribute, not the arithmetic around it, so the enclosing expression has to be
  folded with the attribute's value substituted. 8 errors in the FPU.
- `attribute of a non-name prefix` (9 in the FPU): `lowerAttr` refuses any
  prefix that is not a NAME, so a record element's `'length`/`'range` is refused
  even where the type is known.

### Compound slice bounds

Closed in `a8670f9`: the fold is a small recursive evaluator
(`Lowering::sliceBound`) that handles `PAREN`, an attribute of the sliced
prefix (`'left`, `'right`, `'length`) and a `+`/`-` around one of those, so
`s(s'left-1 downto 4)` folds. `t_vhdl_attr_slice` checks both
`s(s'left downto 4)` and `s(s'left-1 downto 4)` and GHDL agrees. neorv32's FPU
loses its last eight non-static slice errors; what is left there is
`attribute of a non-name prefix` (11), which is `lowerAttr`'s NAME-only
restriction.

### Attribute of a record element

Closed: `lowerAttr` required a NAME prefix, so `r.mant'length`,
`r.mant(r.mant'left)` and `r.mant(r.mant'left-1 downto 0)` were all refused. A
prefix that is not a name now resolves through `typeOfDesignator`, and the
static attributes come from that type -- the same low/high/left/right/length
path a declared name already used, so no second implementation was added.
`t_vhdl_rec_attr` checks all three forms against GHDL and fails without the
change (`attribute of a non-name prefix`).

neorv32's RTL now lints with **no frontend gaps left**: every error in the
checked tree is a genuine mismatch between the sources in that partial checkout
(`entity 'neorv_bus_reg' has no port 'clk_i'` and friends), which the VHDL
tooling there is expected to disagree about, not a refusal.

### N41 measured, fix reverted (not landed)

Root cause established: `lowerProcedureCall` inlines a concurrent call's prologue, local
initializers and body as a flat statement list added straight to the module
(`emitConcurrent`), and each signal statement was wrapped in its own
`AstAlways{CONT_ASSIGN}` (`lowerAssign`'s `m_concurrentProc` case). A module-scope
`AstAssign` -- the procedure's variable write -- is what `V3Delayed.cpp:577` rejects, so
any body with a variable or control flow faults; only a body of one signal assignment per
formal survives, which is why `/tmp/n38/p15.vhd` was clean.

The direction that works structurally (verified, then reverted): lower the whole inlined
list into one `AstAlways{ALWAYS, senTree}`, and build the sensitivity by walking **every**
statement of the list -- `resultp->foreach` sees only the first statement's subtree, and a
partial list makes V3Active classify the block as combinational (`m_allChanged`, no timing
control) so the signal writes draw `COMBDLY` and become blocking. With the full walk the
ALU reproducer lints clean, no warning, no Internal Error; `m_concurrentProc` and
`lowerAggregateAssign`'s flag (renamed `continuous`, still needed by the concurrent
aggregate assignment that really is a continuous assignment) cover the rest.

What blocked landing it: no ground truth for the values. GHDL analyzes the same source but
its equivalent-process model never evaluates the body (it triggers only on an event of the
*signal* formals, which only the body drives), so it reports the outputs as uninitialized
while the frontend computes numbers -- `t_vhdl_proc_call`'s checker disagrees on all three
outputs, so it cannot pin a value and a lint-only test would pin nothing. The finding stays
open with `t_vhdl_proc_var_write_bad` unchanged; landing the change needs a value reference
for the resulting schedule first (another simulator, or a shape whose signal-formal actual
is driven from elsewhere so GHDL does trigger the call).

## P9 - corpus sweep 3 (2026-11-14)

Swept every non-testbench VHDL of the 20 cores at once (`/tmp/vlf/sweep3.py`,
`--language vhdl --lint-only -Wno-MULTITOP --error-limit 0`, each refusal classified
`rtl`/`sim`/`vendor`). **RTL refusals: 251 at session start, 201 at the end.**

| core      | start | end | note                                        |
|-----------|------:|----:|---------------------------------------------|
| patmos    |    99 |  99 | argo packages absent + `std_logic_misc`      |
| poc       |    51 |  39 | parse cluster: physical types, subtype forms  |
| neo430    |    45 |   4 | folded-call constants (slice bounds)          |
| potato    |    17 |  13 | `while`/`**` folds done; record writes left   |
| neorv32   |    24 |  21 | textio region skipped                         |
| t80       |     6 |   6 | `std_logic_misc`, case without `others`       |
| bit-serial|     5 |   5 | sequential `after`                            |
| hdl-modules|    3 |   3 | `context`, 2-D sine tables                    |
| microwatt |     3 |   2 | `alias` in a process                          |
| open-logic|     2 |   2 | `file` formal of a subprogram                 |
| others    |     6 |   5 | rpu, neotrng, fpga_puf, spi-master, vhdl-lib  |

Closed this session: elaboration order (N41), a generate's declarative part (N42),
length-prefixed bit-strings (N44), the full `time` unit set (N45), the `while` fold
and the per-iteration loop bound (N46), static-only-after-lowering bounds (N47),
unconstrained multi-dimensional arrays and `t(r1, r2)` (N48), comment pragmas (N49).
Closed since (2026-11-15): physical types with user-declared units (N50), see P10.

Worklist, frontend-only and corpus-evidenced, biggest first:

- [x] ~~**User-defined physical types (N50)**~~ done: the units of every physical type
      are collected over the library into `Design::unitFactors`; the literal keeps its
      unit name and the unscaled abstract literal, and lowering scales it. Not
      modelled: `'high`, `'low`, `'image` of a physical type (`time` has the same gap).
- [ ] **poc parse cluster** (16 of the 20 left, see P10): `open` index constraint
      (`axi4stream.pkg.vhdl:216`, `drp.generic.vhdl:46`), a chained constraint whose
      second group names record elements (`axi4lite.pkg.vhdl:201`,
      `axi4_DeMux.vhdl:202`), `'element` (`arith_Convert_Binary2BCD.vhdl:154`),
      `array (a, b)` in a range (`axi4_Full.pkg.vhdl:354`), `expected 'generate',
      got ':'` (`poc:2`).
- [ ] **Record-element write in an inlined function body** (potato 5):
      `pp_types.vhd:103-121` writes `r.field := ...`; `evalFuncBlockBody` accepts a
      name, an index and a slice target but no record element.
- [ ] **Sequential `after`** (bit-serial 4): `bit.vhd:118-122`, `peripherals.vhd:133`,
      `uart.vhd:296`, `util.vhd:250`. The concurrent form is modelled (N37); the
      process form is not.
- [ ] **`alias` in a process declarative part** (2): `microwatt/sim_jtag.vhdl:18`,
      `vhdl-lib/SDCard.vhd:268`.
- [ ] **Non-static selected-assignment choice** (rpu 2): `unit_alu_RV32_I.vhd:219`,
      `unit_decoder_RV32I.vhd:247`.
- [ ] **Constant initialized by an impure function** (neo430 2):
      `neo430_boot_rom.vhd:77` `init_boot_rom`, then `boot_img` unknown. Related: a
      recursive pure fold (neorv32 `neorv32_top.vhd:317`) and `'image` with an
      argument (`neorv32_cpu.vhd:109`).
- [ ] **`context` declarations** (2): `hdl-modules/.../common_context.vhd:20`,
      `poc/src/common/common.vhdl:38`.
- [ ] **`file` as a subprogram formal** (open-logic 1): `olo_fix_pkg.vhd:88-95` --
      kills the package for every importer.
- [ ] Open, not chased: `inout` concurrent assignment (N52, 8),
      `IEEE.std_logic_misc` `shl`/`shr`/`to_x01` (N51, 19), microwatt
      `unexpected architecture declaration 'wb_out'` (`no-init-mem.vhdl:19`).
- [ ] Sweep artifacts, not frontend gaps: patmos argo imports `work.ocp` and
      `work.argo_types`, absent from the repository (~60), `Duplicate VHDL entity`
      from board variants (8), `generic without a default value` because no top is
      given (21), `Cannot find VHDL entity` for files outside the sweep's list (6).

## P10 - physical types (2026-11-15)

Re-swept after N50 with the same command (`/tmp/vlf/sweep3.py`): **RTL refusals 201 →
182**, every one of them in poc (39 → 20). The family
`expected ';' or ')' in a generic clause, got 'X'` (15, poc generic defaults such as
`CLOCK_FREQ : FREQ := 100 MHz`) is gone; `type definition 'X'` 2 → 1 (the one left is
`protected`); `expected ')', got 'X'` 12 → 10 and `expected ';' after a declaration`
3 → 2. Corpus `make lint|run|trace` unchanged: 0 lint errors, 11 run scenarios, 4
cycle traces.

poc's remaining 20 RTL refusals:

- `expected ')', got '('` (4) `sortnet_{BitonicSort,OddEvenMergeSort,OddEvenSort,Transform}`
- `expected ')', got ','` (3) `stat_{Histogram,Maximum,Minimum}`
- `expected ';' after a subtype declaration` (3) `axi4stream.pkg:216`,
  `drp.generic:46`, `vectors:100`; `expected ';' after a declaration` (2)
  `arith_Convert_Binary2BCD:154`, `stream.pkg:309`
- `VHDL multi-dimensional array type is not supported yet` (2) `axi4_DeMux:202`,
  `axi4_Mux:364`
- `expected 'generate', got ':'` (2) `ocrom_{DualPort,SinglePort}`
- one each: `type definition 'protected'` (`protected.v08:34`),
  `expected 'to' or 'downto' in a range, got ')'` (`axi4_Full.pkg:354`),
  `expected ';' or ')' in an interface list, got '('` (`axi4lite.pkg:201`),
  `unexpected token 'end' at design-unit level` (`common:38`, the `context`)

## P11 - index-range attributes and index constraint lists (2026-11-15)

Landed as three commits (`08da791`, `177c45d`, docs `b2383b7`):

- `A'LEFT(N)` .. `A'RANGE(N)` take their level in a constraint, a slice and a loop
  range; one helper (`Lowering::indexRangeLevel`) resolves it for every consumer
  (`evalInt`, `constraintBounds`, `lowerAttr`, `lowerSel`, `lowerFor`,
  `evalFuncFor`).
- A constraint list `t(v'range, n - 1 downto 0)` feeds the same level list as the
  chained `t(v'range)(n - 1 downto 0)`; `open` leaves an index to the object, so
  `subtype S is T(open)(1 downto 0)` constrains only the element; a chained group
  may be an attribute range (`t(v'range)(v(v'low)'range)`), and a designator may
  prefix a range attribute.
- A folded function body is bounded by what it copies (`m_funcWriteCost`,
  `64 + width/64` per element write, limit 2^18). Without it, `stat_Histogram`'s
  `to_slm` over a 65536-entry memory reached 90 GB before answering.

Tests: `t_vhdl_array_attr_index`, `t_vhdl_index_constraints`,
`t_vhdl_fold_budget_unsup` (negative). Native suite 152/0/1, GHDL path
103/3/47. Sweep: RTL refusals 182 -> 175 (poc 20 -> 19, potato 13 -> 7);
after the constraint work `vectors.vhdl` clears (poc 18 by the per-file count).
`/tmp/vlf/sweep3.py` is gone (tmp cleanup) - the 182/175 numbers are the ones
measured before it vanished.

poc refusals left (per-file count, 2026-11-15):

- `t(open)(Field(r), ...)` record constraints: `axi4stream.pkg:216`,
  `drp.generic:46` (`multi-dimensional array type`), `axi4_Full.pkg:354`
  (`expected 'to' or 'downto' in a range, got ')'`), `axi4lite.pkg:893`,
  `stream.pkg:751`
- `'element` as a type mark: `arith_Convert_Binary2BCD:154`
- `context` declaration: `common.vhdl:38`
- `protected` type: `protected.v08:34`
- `stat_*`, `sortnet_*` need `utils`/`vectors` plus `to_unsigned` bodies and hit
  the fold budget (they lints clean only with their packages)
- `ocrom_*`: `gen: if cond: (X = Y) or ... generate` - check GHDL first

## P12 - record constraints and 'element (2026-11-15)

Landed: `e5e3fc6` (record constraint on a nested element) and `3289c0b` (`'element`
type mark), docs `2c9e23a`, `9c5854a`.

- A record with unconstrained elements is constrained where it is used:
  `rec_t(f(3 downto 0), g(x'range))`, also chained after `open`
  (`t_arr(open)(f(r))`). Told apart from an index range by a range inside the
  member's own parentheses, so `t(v'range)(v(v'low)'range)` is still two index
  ranges.
- `T'element` is the element type of an array, whether `T` is a type or an
  object (poc declares a register as `BCDDigits'element`, naming a port).

Tests: `t_vhdl_record_constraint`, `t_vhdl_element_type` (values confirmed in
GHDL). Native suite 154/0/1, GHDL path 103/3/49.

poc per-file lint: 19 -> 12 refuse; clean now are `vectors.vhdl`,
`axi4stream.pkg`, `drp.generic`. Of the 12, ten fail only on names a sibling
package declares (`t_slm`, `t_axi4_*`, `t_slv_8`, `t_bcd_vector`) -- an artifact of
linting one file at a time, not a frontend gap. The two real gaps left in poc:

- `context` declaration (`common.vhdl:38`)
- `protected` type (`protected.v08:34`)

Still to check: the ocrom `gen: if cond: (X = Y) or ... generate` shape (GHDL
first), and N54 (indexing an array a function returns reads one bit).

## P13 - context declarations and labelled generate conditions (2026-11-15)

Landed: `6de3383` (skip a context declaration), `12d75c8` (a generate condition
with a label), docs `4c18123`, `b2f3e9d`.

- `context c is ... end context;` is a bundle of clauses and no design unit, so it
  is skipped whole. The skip helper also serves attribute and disconnect
  specifications, so the declaration form is only looked for where the clause
  really starts with `context` -- the first version read `default clocking is ...`
  as one and ate the next process.
- `if label: condition generate`: the label is a name and nothing else, and GHDL
  and nvc both take the expression after it as the condition (probed by simulation:
  `if false: true` takes the then-branch, `if true: false` the else). The frontend
  follows them. poc's ocrom ROMs use it; they now stop at a generic with no
  default value and no instance to bind it, which is a sweep artifact.

Tests: `t_vhdl_context_decl`, `t_vhdl_labelled_cond` (values confirmed in GHDL
and nvc). Native suite 156/0/1, GHDL path 105/3/49.

poc per-file lint now: 7 files refuse, 6 of them only on names a sibling package
declares (`t_axi4_response`, `t_axi4_cache`, `t_slv_8`, `t_bcd_vector`,
`t_slm` twice) -- an artifact of linting one file at a time. The one real gap
left in poc is the `protected` type (`protected.v08.vhdl:34`).

Still open: N54 (indexing an array a function returns reads one bit -- a silent
wrong value, recorded in findings.rst, not fixed).

## P14 - indexing an array a function returns (2026-11-15)

Landed: `13e5936` (N54 closed), docs `2d8c5d6`.

`f(x)(0)` read one bit, because a designator could not type a call: the value's own
data type is a flat vector. The declared return type now types the designator, but
only where the mark names a declared array type -- an unconstrained
`std_logic_vector` return has no element width to give, and taking one from it
turned a wrong value into an error for `t_vhdl_slice` (caught by the suite). A
conversion still selects a bit, which is what it means.

Test: `t_vhdl_func_result_index` (values confirmed in GHDL; the GHDL XML path reads
a bit there, so it is native-only). Native suite 157/0/1, GHDL path 105/3/50.

## P15 - corpus sweep with the native frontend (2026-11-15)

Tooling now lives in the corpus repo: `../vhdl_references/tools/lint_sweep.py`
(`--group file|core`, `--json`, per-file `ulimit -v` and timeout, artifacts split
from real refusals). Sweep of 1209 design files (testbenches excluded):

- Per core (18 units): 7 refused, 5 of them only answerable with the whole design
  (missing sibling packages, a top with no instance to bind its generics). Two
  refusals on the design alone: `vhdl-lib/AdcInterfaces.vhd:95` (a top with
  unconstrained vector ports -- cannot be sized) and `bit-serial/bit.vhd:142`
  (a textio `line` variable in a debug procedure -- not synthesizable).
- Per file: 1041 refused, 765 of them artifacts. The largest RTL families are all
  unresolved names from sibling packages, which the per-file method cannot see.

Landed from it: `9d69226` (`after <delay>` with a zero delay is a delta and is
accepted; a longer one inside a process is reported), docs `2cf1afa`, plus
`t_vhdl_after` and `t_vhdl_delay_unsup`. Native suite 159/0/1, GHDL path 106/3/51.

Not chased: `t_vhdl_func_result_index`'s sibling case (a call result whose return
type is an unconstrained `std_logic_vector` still selects a bit), and the file-I/O
subset (`line`, `textio`, `access` types).

## P16 - what the corpus sweep left (2026-11-15)

Final state of `tools/lint_sweep.py --group core`: 18 cores, 7 refused, 5 of them
only answerable with the whole design. Two refusals on the design alone, both
correct: `vhdl-lib/AdcInterfaces.vhd:95` (a top with unconstrained vector ports)
and `bit-serial/bit.vhd:142` (a textio `line` variable in a debug procedure).

Landed since: `ff84036` (an index of an array whose element has no width was a
crash -- `lowerSel` built a zero-width select and the emitter faulted; now
reported), docs `ba00869`, and `2ef573e` (the simulation-only subset recorded as
deliberately refused: `line`/textio/access types and poc's `protected` type).
`t_vhdl_elem_unsup` fails with an internal fault without the fix.

Nothing is left on the todo list: the remaining refusals in the corpus are
artifacts of linting one file or one core at a time, a top that cannot be sized,
or constructs that have no synthesizable meaning.

## P17 - performance versus GHDL (2026-11-15)

Measured on this machine, wall time, `ulimit -v` 6 GB, 8-way parallel where noted.

Frontend throughput, 1681 corpus files (`*.vhd`/`*.vhdl` under `third_party/`),
one process per file:

| tool | time | files/s |
|---|---|---|
| native `--lint-only --vhdl-frontend native` | 13.46 s | 125 |
| `ghdl -a --std=08` | 4.75 s | 354 |

Per design (poc-divider, neotrng, hdl-modules-lfsr), cold build:

| step | GHDL | native |
|---|---|---|
| analyze + elaborate | 0.65 / 0.25 / 0.37 s | 0.06 / 0.05 / 0.06 s (lint) |
| code generation | 0.09 s (`-e`, poc-divider) | 6.6 s cold, 0.19 s warm (g++ via ccache) |
| run, 10 iterations | 0.80 / 0.08 s | 0.036 / 0.027 s |

The per-run numbers are dominated by process startup; the simulated times are
86.1 us (poc-divider), 112.4 us (neotrng) and 12.2 us (hdl-modules-lfsr).

Not comparable: a time-based VHDL testbench. GHDL runs a 1 ms clocked counter in
0.02 s; the native `--binary --timing` model does not stop on
`assert ... severity failure` (reproducer: a process that waits 1 us then asserts
failure runs forever, `timeout` 124) and a free-running clock never ends. The
corpus harnesses are all cycle-based C++ for this reason.

Found, not fixed: `severity failure`/`error` does not end a timing simulation.

## P18 - five RTL shapes from whole-core linting (2026-11-15)

Landed: `154fa50` (entity statement part), `948ea8a` (case leaving no value on
some path), `d23a826` (choice that is an expression of constants), `ccab6d4`
(alias in a process declarative part), `bc37b5f` (selected entity name in an
instantiation); docs `25bca65`; findings N61-N66.

Tests: `t_vhdl_entity_assert`, `t_vhdl_case_cover`, `t_vhdl_choice_expr`,
`t_vhdl_proc_alias`, `t_vhdl_selected_inst`; `t_vhdl_fn_case_bad` golden updated
(the function now genuinely falls through without returning). Native suite
168/0/1, GHDL path 111/3/49 -> 55 skips.

Bugs found in my own edits, both fixed: two lexer lookaheads restored the lexer
but not `m_cur` (the instantiation lookahead produced `expected ';' after a
procedure call, got 'generic'`), and the first case fix changed an existing
golden rather than the construct's real diagnosis.

Last remaining RTL item: `neorv32/rtl/core/neorv32_cpu.vhd:109` -- `'image`
with an argument building a trace file name (`"neorv32_trace" &
natural'image(HART_ID) & ".log"`), which is simulation-only but sits in an RTL
file. After it, poc's `protected` type, file I/O, and 64 KB memory images are
the only refusals left, and all three are deliberate limits.
