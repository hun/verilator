.. DESCRIPTION: Verilator: native VHDL frontend coverage assessment
   SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
   SPDX-License-Identifier: CC0-1.0

####################################
VHDL frontend coverage assessment
####################################

This page assesses what the in-tree VHDL frontend accepts, where it models VHDL
correctly, and which gaps remain. It is a snapshot, not a specification: the
feature scope is :file:`vhdl_synth_subset.md` in the repository root, and the
architecture is :doc:`architecture`. The assessment below was made against
Verilator 5.055 devel on branch ``vhdl-native-frontend``, using GHDL 7.0.0-dev
as the independent reference.

____________
Overall call
____________

The frontend covers a large and useful part of synthesizable VHDL-2008 and
elaborates real designs, but its semantic analysis is deliberately shallow
outside the pinned idioms. The main risk is not an unsupported construct: it is
a construct that is accepted and lowered with a different meaning. Assignment,
return, single-candidate call-actual, generic-actual, type conversion, operator,
standard-intrinsic and edge-function paths now check that a value belongs to the
type the standard requires. The 2026-11-20 conformance audit closed the
remaining findings with regression tests. The model limits that remain are the
two-state collapse, the ``report`` fallback text, the coarse array conversion,
and the inout contract described below.

The in-tree frontend is the only VHDL frontend; tests do not invoke GHDL or
nvc. Those simulators are used only in external corpus sweeps that are not
reproducible from this checkout.

__________________
Measured coverage
__________________

The regression suite contains 323 VHDL tests; all pass (re-run on 2026-11-20).
The GHDL ``synth`` testsuite corpus is used as a breadth check with
``--lint-only`` on 1139 case/top pairs, re-measured on 2026-11-19 with the
current tree. The corpus is not part of this repository, so that sweep cannot
be repeated in-tree; the counts below are retained from that run.

========================================  =====
Result                                    Count
========================================  =====
PASS (accepted and lowered)                 865
UNSUPPORTED (clean diagnostic)              117
ERROR                                       103
EXPECTED_FAIL                                38
NO_TOP                                       16
========================================  =====

Acceptance is not correctness: the corpus is lint-only, and a design counts as
PASS when the frontend accepts it, not when its simulation matches VHDL. The
value-checked evidence is the 323 regression tests, 20 ported corpus designs
whose results are compared against GHDL, and the specific probes in this
assessment.

Of the 103 corpus errors, 40 are "entity not found" (mostly corpus-runner
selection artifacts), 4 are entities without an architecture, 19 are parser
refusals of syntax the frontend does not model, 11 are unknown identifiers,
mostly standard subprograms, and 10 are driver-count diagnostics. Eight of those
ten are false positives: two concurrent drivers on a scalar ``std_logic``, or on
a record or custom array built from it, are reported as unresolved because the
type resolver records both ``std_logic`` and ``std_ulogic`` as ``std_ulogic``,
so the resolved subtype can no longer be told from the unresolved one
(``std_logic_vector`` keeps its name and escapes this). In addition,
3 are case-coverage diagnostics and 7 are other frontend diagnostics. Eight
case/top pairs are not answered by the frontend at all: four zero-width designs
fail in ``V3Width``, one ``bit`` ``inout`` fails the net-type check, and three
legal designs are rejected by ``BLKANDNBLK`` or ``ASSIGNIN`` in the middle end.
One case reaches an internal error. `Internal faults and middle-end leaks`_
describes those nine.

A sweep of 18 real designs with whole-design linting reports 143 refusals in 39
files; 54 of those need the whole design to answer. The neorv32 core
(``rtl/core/*.vhd``, top ``neorv32_top``) lints clean; the whole ``rtl/`` tree
with its stock Verilog wrapper still aborts with ``std::bad_alloc`` under an
8 GiB limit.

__________________
Feature coverage
__________________

===========================================  =========  =====================================
Area                                         Status      Notes
===========================================  =========  =====================================
Entities, architectures, packages            Supported  Multiple architectures: last analyzed
                                                         wins; context declarations are expanded
Components and direct instantiation          Supported  Configuration units are validated,
                                                         not applied; component interfaces are
                                                         not checked, and an instance's
                                                         (ARCH) selection is ignored (audit)
Ports, generics, generic types               Supported  Unconstrained ports specialize per
                                                         instance; inout ports use V3Tristate;
                                                         scalar actuals/defaults are unchecked
                                                         and a missing input is silent (audit)
Scalar, enum, record, 1-D array types        Supported  Includes arrays of arrays; multi-
                                                         dimensional ports excluded
Static real and physical types               Supported  Elaboration-time constants only
Expressions and aggregates                   Supported  Two-state model limits below;
                                                         operator chains and mixed array
                                                         base types are accepted (audit)
Sequential statements and subprograms        Partial    Procedures/functions inlined; dynamic
                                                         loops, impure functions, signal formals
                                                         limited
Concurrent statements and generate           Supported  Conditional assignment with no final
                                                         ``else`` is a latch holding the target
``case`` generate, matching ``case``         Excluded   Legal VHDL-2008 forms; they
                                                         are refused with a parser error
Subtypes and type attributes                 Partial    Enumeration range subtypes and
                                                         ``'base``/``'subtype``/``'element``/
                                                         ``'simple_name`` are refused
``numeric_std`` and ``std_logic_1164``       Partial    Arithmetic and conversions; not every
                                                         function; see library gaps
``std_logic_arith``, ``std_logic_signed``    Partial    Conversions, comparisons and
                                                         ``shl``/``shr`` work; signed
                                                         operators on ``std_logic_vector``
                                                         are modelled unsigned
``math_real``                                Partial    Locally static constants only
Access, protected, file, textio runtime      Excluded   File contents can be read at
                                                         elaboration; runtime objects are refused
Timing, multiple waits, external names       Excluded   Single top-level wait and timed
                                                         testbench constructs are supported
Mixed VHDL/SystemVerilog sources             Partial    Both languages in one invocation, and the
                                                        --lib-create library boundary; a VHDL
                                                        instance binds only VHDL entities
===========================================  =========  =====================================

Where the implementation is sound
=================================

- The conformance review against IEEE 1076-2008 (findings N99..N126, all closed
  with GHDL/nvc-confirmed tests) removed the value divergences it found and added
  the analysis-time checks the frontend lacked: case and selected choice lists
  must be complete and free of duplicates, an unresolved signal may not have two
  drivers on the same bit, integer literals and constants respect their ranges,
  ``'val`` is bounds-checked, division by zero is reported, associations cannot
  mix forms, whole-port actuals must conform, and illegal identifiers,
  reserved words and a wait in a sensitized process are refused.
  The findings table records each one with its test. Type conformance is now
  checked separately by one predicate on the assignment, declaration, return,
  single-candidate call, generic and conversion paths (see
  `Type conformance`_). The 2026-11-20 audit covered the port, subprogram-class,
  selector and nested-type cases as well; each closed item and its regression
  test are recorded in `Audit findings`_.

- Elaboration is the strongest part: generic binding and specialization,
  unconstrained ports, generate unrolling, package bodies and deferred
  constants, record and array packing, and component binding are exercised by
  the suite and by ported corpus designs.
- The clocked-process model is correct for the supported idioms: explicit
  sensitivity lists, ``rising_edge``/``falling_edge`` and ``'event`` clock
  tests, synchronous and asynchronous resets, ``wait until`` splitting,
  ``process(all)`` read inference, element sensitivity items such as
  ``process(rec.field, v(0))``, and time-zero activation. The asynchronous
  reset idiom is emitted as a true edge-triggered block.
- ``numeric_std`` arithmetic follows VHDL for widths, sign extension, division,
  ``mod``/``rem``, ``resize``, and the shift/rotate functions on ``unsigned``
  and ``signed``. Overflow and runtime range checks are intentionally absent;
  integer arithmetic is 32-bit. ``std_logic_arith``'s comparisons widen an
  unsigned operand by one bit and compare as signed, and its ``shl``/``shr`` are
  logical for both signednesses.
- Slicing, indexing, ascending and descending ranges, records, and aggregate
  targets are value-tested. A two-dimensional element target is typed one array
  level per index, so element writes use the element width.
- Concurrent conditional assignments with no final ``else`` or an
  ``unaffected`` branch hold the target, and an assertion inside a concurrent
  procedure call is evaluated continuously. Both are value-checked.
- Repeated aggregate initializers are linear: a constant ``(others => value)``
  becomes one constant, and a run-time repetition is chunked below the
  replication limit. A 2^16-element nested-zero memory lints in 0.05 s.
- Inout and tristate: scalar and vector ports, concurrent conditional and
  selected drivers, process drivers, hierarchical connections, and the top-level
  enable/drive interface are tested. Conflicting drivers remain a documented
  two-state divergence.
- Most unsupported constructs are refused rather than silently accepted: 238
  sites use the shared refusal helper, which carries a source location and
  names the construct. The emitted form is
  ``%Error: ... Unsupported: VHDL <construct>`` (an ordinary error, not
  ``%Error-UNSUPPORTED``), so the shipped corpus runner does not classify those
  cases as UNSUPPORTED. A few legal forms (matching ``case``, a resolution
  indication) instead reach a generic parser error.

Semantic divergences
====================

The model divergences come from the two-state model: ``is_x`` is always
false, and a signal cannot hold a metavalue. ``'Z'`` is preserved for a
tristate driver and resolves to zero anywhere else. Array ordering of
metavalue constants uses the two-state order, not the IEEE 1164 enumeration
order. An object that the source neither initializes nor resets starts at 0 in
the model, including an ``integer``, where IEEE 1076-2008 6.4.2.1 gives it
``T'left``; :file:`vhdl_synth_subset.md` 5.1 documents the choice. These are
documented model limits, not silent miscompilations. They are separate from the
type-conformance hole below, which is not a model limit: it accepts programs
the standard rejects.

One statement-level divergence remains but is no longer a termination bug: a
``report`` whose message is not a string literal executes with a fixed fallback
text. ``report "x" & bit'image(y) severity failure`` now reports and terminates;
the literal form prints its own text. Probed on 2026-11-20 with a C++ harness,
which saw ``gotFinish`` set, and GHDL 7.0.0-dev stops on the same computed
source. ``t_vhdl_report_finish`` pins the termination; the message text is not
modelled. A computed assertion message uses the default text too, but the
assertion still runs and still terminates.

A metavalue choice is legal VHDL, but the two-state model has no ``'X'`` value
to match. The frontend now refuses such a case or selected choice with a
source-located diagnostic instead of asserting in ``V3Number::toUInt``;
``t_vhdl_case_metavalue_unsup`` pins both the scalar and vector forms.

The former overload and bound-check divergences are closed:

- A typed context resolves an enumeration literal, including when two visible
  types share a literal name. Without such a context, a shared literal is an
  ambiguity error rather than the last declaration silently winning.
- Two polymorphic operands are matched by their bit-string widths and the
  expected result type. If neither operand is a sized literal, the built-in
  operator applies as before.
- A type conversion has the type of its mark, constrained by its operand, so an
  operator declared over an unrelated type is no longer selected for the
  built-in operator; a user operator still wins where its formals match.
- An element write and an aggregate operand use the declared type rather than the
  packed vector: a record element of an array local takes a positional aggregate, and
  an aggregate beside a wider vector keeps the bounds its own choices name.
- A dynamic ``'succ``/``'pred`` of an end enumeration literal is checked at run
  time and reports an error and finishes; a static one is a frontend error.

Array equality works for any element type; ordering is lexicographic for an
array of a discrete element type, and an array whose element is not discrete is
rejected because VHDL defines no ordering for it.

Two library caveats deserve emphasis. Unknown standard packages are treated as
intrinsics instead of being refused; ``std_logic_unsigned`` matches the modeled
unsigned behavior, while an arithmetic or comparison operator under
``std_logic_signed`` is refused. Arithmetic on a ``std_logic_vector`` is itself
refused unless ``std_logic_unsigned``, ``std_logic_arith`` or
``std_logic_signed`` is visible, since the predefined operators have bit and
boolean elements; shifts stay legal because ``std_logic_1164`` (2008) declares
the ``std_logic_vector`` ones. Explicit infix and prefix operators and
subprogram overloads are selected by base parameter type, including
``unsigned`` versus ``signed``, distinct array types, and package-qualified
type names. An integer actual selects an integer overload over a real one; a
real formal still accepts an integer literal when it is the only candidate. A
`string` formal accepts string actuals and string constants, and a discrete
subtype names an array's index range (`unsigned(Addr_c)`). A loop whose range is
only known at run time runs as a loop and its `exit` latches a flag the loop test
reads, a slice assignment whose width is a run-time value becomes one guarded
write per width, and a conversion whose length is a run-time value takes the type
its consumer needs. An input port's default may name the entity's own generics,
and an instance that binds only some of them fills in the rest. A constant that
calls a function another package defines in its body waits for the pass that reads
the bodies, and a body sees the constants its own package body declares. A
constant of an unconstrained array type takes its bounds from its aggregate, an
overload accepts a constrained actual for an unconstrained formal of the same base
type, and an array whose element type leaves a member unconstrained is reported by
name. A declaration of a unit hides an imported one of the same profile, a package
publishes only what it declares, an inlined body keeps its package's private
subprograms in scope, a design file's `use` clause belongs to one library unit and
its secondaries, and an expression that converts through a real folds at elaboration
time. A branch a static condition disables is not lowered, a fold never reads an
aggregate or a wide value as a 32-bit integer, the predefined arrays of a scalar
(`integer_vector`) are an array of that scalar whose bounds a function result or an
aggregate supplies, a lowered constant keeps its type for overload resolution, an
operator is typed by its operands, a string literal of logic characters is one
character per element in a string context, and an impure function whose body opens no
file inlines at elaboration time, and a bit-string literal beside an unsigned or signed
vector keeps that vector's signing, and a subprogram of a declarative part sees the
constants declared before it, and an enumeration value indexes an array by its
position. A
string literal keeps a doubled quote as one quote character, and a case choice
that pairs literals the two-state model merges (`'0'` with `'L'`) is one
choice rather than a duplicate. A
generic of type ``real`` binds like a real constant: its value folds at
elaboration, so the model never carries a floating type. A call of a package
subprogram through a library-qualified name (``work.pkg.f(...)``) resolves to
that package, as an unqualified call does after a ``use``. An empty string is the
null string ``1 to 0``: its bounds and length are values, and a function that
takes it loops over a null range.
When one operand is polymorphic, the selected formal gives it a
type; two polymorphic sized literals are resolved by width and result type. A
type declaration may take its element range from an object of the same
declarative part, resolved once that part's objects are in scope, and a
subprogram's local type may be bounded by a constant of that subprogram. When an
inlined call has a statement sink, its array locals are real variables, so a
body that rewrites them element by element folds in linear space instead of
copying its value per write. An aggregate actual has no type of its own, so it
takes the base type the enclosing expression needs: the typed operand of an operator is the context of
an untyped operand beside it, and the context selects the overload whose return
type matches. A choice that names an object's ``'range`` gives the aggregate its
width where it is the only thing that can.
Multiple exact candidates are reported as ambiguous. Importing two packages
that export the same type name unqualified is an error; the qualified mark
selects one.

Type conformance
================

Assignment, declarations, return, single-candidate call actuals, generic
actuals and type conversions now check that a value belongs to the target's
base type. The check is one predicate, ``valueConforms``, and it applies to
plain, conditional, selected, latched and clocked assignments, signal and
variable initial values, constants, inlined returns, generic associations and
conversions. The operands of logical and relational operators are checked by
``logicalOperandsConform`` and ``relationalOperandsConform``. A single visible
subprogram is no longer exempt from its formals' types. Every illegal program
below is refused with a source-located diagnostic and pinned by
``t_vhdl_type_conf_bad``:

- a 4-bit vector assigned to an 8-bit vector of the same base type;
- an integer literal assigned to ``std_logic``, and a vector assigned to an
  integer;
- ``std_logic_vector`` assigned to ``unsigned``, and ``boolean`` assigned to
  ``std_logic``;
- ``return 1`` from a ``std_logic`` function;
- ``f(5)`` where ``f`` declares a ``std_logic`` formal;
- a string actual associated with an integer generic;
- ``std_logic_vector(42)``, ``integer(slv_value)`` and ``time(5)``;
- an enumeration literal that the target type does not declare;
- a logical operator on ``std_logic`` and ``boolean``, ``not`` on an integer
  or a string, and ``std_logic_signal = integer_signal``,
  ``boolean_signal = std_logic_signal`` or ``array = scalar``.

The checks also learned what VHDL permits, so the following stay accepted:
a universal integer literal takes an integer subtype or a real target, and an
integral real literal such as ``25e6`` takes an integer subtype;
``std_logic_vector`` and ``std_ulogic_vector`` are related (IEEE 1164); ``+``
and ``-`` keep a physical type, and a physical value multiplied or divided by
an integer or real keeps it; ``??`` yields boolean; a matching operator yields
``std_ulogic``; ``'high``/``'low``/``'left``/``'right`` return the prefix's
scalar type, or an array's index (an integer here), while
``'length``/``'pos``/``'to_integer`` return integer. A logical operator may mix
an array with a scalar of its element type, and a character literal takes the
other operand's element type. A relational operator may compare an
``unsigned``/``signed`` vector, or a legacy numeric-package vector, with an
integer. ``'event``/``'active``/``'stable`` and ``rising_edge`` yield boolean.
Whole-port association checks vectors and the shape of a simple designator
(N117), but it does not check a scalar actual's base type, and it does not run
on a port default or on an unassociated input.

The conformance holes found by the 2026-11-20 audit are:

- A scalar port actual, and a port or generic default, may have any base type;
  the value is resized instead of refused.
- A named input with no default, or an ``open`` input with no default, is left
  unconnected without a diagnostic.
- A function call used as a statement and a procedure used as a value are
  dropped silently.
- A character literal is treated as untyped for any target, so a logic
  character literal reaches an integer, real or boolean context.
- Logical, concatenation and arithmetic operators compare element kinds rather
  than array base types, so ``unsigned`` and ``std_logic_vector`` mix.
- A relation, shift or unary operator chain is accepted without parentheses.
- A ``case`` or ``with`` selector need not be discrete, and an empty ``case``
  is accepted.
- Conversion relatedness between arrays is coarse: any two arrays convert, even
  with different element types.

Standard intrinsics no longer accept arbitrary operands: ``rising_edge`` and
``falling_edge`` require a scalar logic signal, and ``to_integer`` requires an
``unsigned`` or ``signed`` operand. ``t_vhdl_intrinsic_bad`` pins the calls that
GHDL rejects. Array comparisons now require one base type, or two numeric
vectors under ``std_logic_arith``; ``t_vhdl_rel_array_bad`` pins
``std_logic_vector = unsigned``, ``unsigned = signed`` and
``bit_vector = std_logic_vector``. Scalar ``std_logic`` and ``std_ulogic`` keep
distinct identities, so the resolved-subtype check can tell them apart.


Internal faults and middle-end leaks
====================================

One legal corpus case reached an internal error, and eight more were rejected by
the middle end. The internal error and two of the leaks are now frontend
answers. The former crash class was closed by:

- an ``else``-less concurrent conditional assignment is emitted as a latch that
  holds the target when no condition holds;
- a concurrent procedure assertion is evaluated in its own ``always_comb``;
- a two-dimensional element target descends one type level per index, so its
  right-hand side is widened to the element width rather than the row width;
- an unconstrained multi-dimensional port or array conversion is reported with
  a source-located ``Unsupported: VHDL ...`` error;
- repeated aggregate initializers fold a constant value into one constant and
  chunk a run-time replication, so ``(others => (others => '0'))`` over 2^16
  elements lints in 0.05 s and 20 MiB instead of aborting;
- a metavalue choice is refused with a source-located diagnostic instead of
  asserting in ``V3Number::toUInt`` (``t_vhdl_case_metavalue_unsup``);
- an assignment to a whole null array is a no-op and is not emitted, so
  ``z <= (others => '0')`` on ``std_logic_vector(-1 downto 0)`` no longer
  reaches ``V3Width`` (``t_vhdl_null_assign``);
- a ``bit`` inout port is refused at the port with a VHDL diagnostic instead
  of leaking into the net-type check (``t_vhdl_bit_inout_unsup``).

The metavalue internal error is closed. A case statement over ``std_logic`` or
``std_logic_vector`` with a choice such as ``'X'`` or ``"XX"`` used to reach
``V3Number::toUInt`` on a four-state constant through the choice-value
bookkeeping in ``lowerChoiceConsts`` and ``addChoiceValues``. The choice is now
refused with a source-located "VHDL metavalue in a case or selected choice is
not supported yet" diagnostic; the corpus case is ``synth/err01/case01``, and
``t_vhdl_case_metavalue_unsup`` covers the scalar and vector forms.

The earlier real/physical crash is closed: a ``real`` or physical type nested in
an array element or record member is now rejected by the recursive type gate in
``rejectUnmodelledObject``, and ``t_vhdl_nested_real_unsup`` pins the diagnostic.

The remaining middle-end leaks are:

- zero-width objects outside a whole-array assignment. A null array that is
  assigned ``(others => '0')`` is now a no-op, but the four zero-width corpus
  designs have not been re-run, and a zero-width object in another position can
  still reach ``V3Width`` ("Width of bit extract must be positive");
- two legal designs are rejected as ``BLKANDNBLK``: a plain combinational
  process assigning an output port, and an alias of an input-port slice;
- one legal inout procedure actual (``neg(mem(n))``) is rejected as
  ``ASSIGNIN`` naming the index ``n`` (``Assigning to input/const variable:
  'n'``).

The ``BLKANDNBLK`` and ``ASSIGNIN`` cases need the external corpus and were not
re-checked; the metavalue, null-assignment and ``bit inout`` probes were.

The pre-width contract is also checked only in debug runs: ``V3VhdlLink``
asserts the emitted shape under ``--debug-check``, which the regression driver
passes but the corpus sweep and an ordinary user run do not. A release build can
therefore carry a malformed dtype into a middle-end pass. The frontend should
refuse each construct where it is lowered, and check the contract
unconditionally rather than only in debug builds.

The full neorv32 ``rtl/`` tree with its stock Verilog wrapper still aborts with
``std::bad_alloc`` under an 8 GiB limit when the wrapper's complete generic set
is specialized. The core alone remains clean.

Audit findings
==============

The 2026-11-20 follow-up audit with GHDL 7.0.0-dev and nvc re-probed the
shipped frontend with ``--lint-only`` and ``--cc``. The items below record
where the frontend accepts or refuses differently from VHDL and the subset;
each has a regression test.

Silently accepted, wrong or missing model
-----------------------------------------

.. list-table::
   :header-rows: 1
   :widths: 6 34 60

   * - WP
     - Construct
     - Observed
   * - 4
     - ``u : entity work.e(rtl)``
     - The architecture selection is parsed and dropped; the last-analyzed
       architecture is used. A selected name that does not exist is also
       accepted. Fixed: the selected architecture is lowered and a missing
       name is an error.
   * - 4
     - configuration specification,
       ``for u : c use entity work.e(rtl);``
     - Skipped wholesale, so the default binding and architecture are used.
       ``disconnect`` shares the skip path and is tier ``X``. Fixed: both are
       refused with a source-located diagnostic.
   * - 5
     - component declaration whose ports differ from the entity
     - The declaration is dropped; the instantiation is checked against the
       entity, so a stale component builds here and fails in GHDL. Fixed: the
       component interface is kept and checked against the bound entity.
   * - 3
     - scalar port actual, port/generic default, missing input
     - Base type and default shape are unchecked. An unassociated or ``open``
       input with no default is silently left unconnected. Fixed: actuals and
       defaults conform, and an unbound input without a default is an error.
   * - 2
     - function call as a statement; procedure in an expression
     - Accepted and dropped, so an assignment can lose its driver.
       ``resolveCall`` returns null without a diagnostic when only the wrong
       subprogram class matches. Fixed: both directions are errors and the
       expression path names the procedure.
   * - 6
     - ``a = b = c``, ``a sll 1 sll 2``, ``not not a``
     - Accepted; only logical operator mixing is checked (N113). Fixed: all
       three non-associative levels require parentheses.
   * - 6
     - ``unsigned`` mixed with ``std_logic_vector`` in ``+``, ``and`` and
       ``&``
     - Accepted and modelled as unsigned; array base type names are not
       compared, only element kinds and signedness. Fixed: the three operator
       families compare the array base type.
   * - 6
     - ``integer_signal = '1'``, ``return '1'`` from an integer function,
       ``generic(n : integer := '1')``
     - Accepted; a character literal is treated as untyped for any target.
       Fixed: it fits only a logic, bit, character or matching enumeration
       type.
   * - 7
     - empty ``case``; ``case``/``with`` over a record
     - An empty case is dropped without a diagnostic, and the selector need
       not be discrete. Fixed: both are errors before lowering.
   * - 8
     - plain ``variable`` in entity/architecture; duplicate statement labels;
       unknown ``severity`` token; orphan package body; duplicate subprogram
       body; signal assignment in an entity statement-part process
     - Accepted without a diagnostic (a duplicate subprogram fails only if it
       is called). Fixed: each is now a source-located error.
   * - 9
     - ``integer'high + 1``, ``2 ** 31``, ``v sll '1'``, ``i'length`` on a
       scalar
     - Static fold overflow wraps silently; the shift count and array-only
       attribute checks are missing. Fixed: all three are errors.
   * - 1
     - ``real``/physical type as an array element or record member
     - Accepted; the first use crashes in ``AstNodeDType::width`` on a null
       dtype (see `Internal faults and middle-end leaks`_). Fixed: the object
       gate walks array elements and record members.
   * - 10
     - null-range object or port (``std_logic_vector(0 to -1)``)
     - Supported as an empty value: a whole-array assignment is a no-op, and
       ``'length`` reads zero. Other uses of a zero-width object are not
       modelled; the subset tier is now ``S``.

Legal forms refused
-------------------

.. list-table::
   :header-rows: 1
   :widths: 6 42 52

   * - WP
     - Construct
     - Observed
   * - 11
     - ``architecture rtl of work.e is``
     - ``expected 'is', got '.'``; ``parseArchitecture`` took one identifier.
       Fixed: the entity may be library qualified.
   * - 11
     - ``type a is array (work.pkg.idx) of ...``
     - ``expected ')', got '.'``; ``parseIndexConstraint`` took one
       identifier. Fixed: a qualified subtype mark is accepted.
   * - 11
     - ``u : comp;`` (component instantiation with no maps)
     - Parsed as a procedure call and reported as an unknown subprogram
       named ``comp``. Fixed: a labelled bare designator is an instance.
   * - 11
     - ``use work.pkg."+";``
     - ``expected ';', got '+'``; a use clause accepted identifiers only.
       Fixed: the final item may be an operator symbol.
   * - 11
     - ``type t is range 0 to 7;``
     - Was refused as ``Unsupported: VHDL type definition 'range'``. Fixed:
       an integer type definition is modelled as an integer subtype.
   * - 11, 12
     - ``--top-module CaseTop`` for ``entity CaseTop``
     - Not found; only the folded lowercase spelling matched, although VHDL
       lookup is case insensitive. Fixed: the request is folded for VHDL.

These findings supersede the N117 port claim and the N112 case-completeness
claim as stated above; the corresponding work packages add the missing checks
without changing the accepted set for legal designs. Work packages 1--13 are
implemented or decided in the tree (tests ``t_vhdl_nested_real_unsup``,
``t_vhdl_subp_class``, ``t_vhdl_port_*``, ``t_vhdl_generic_shape_bad``,
``t_vhdl_inst_arch*``, ``t_vhdl_cfg_spec_unsup``,
``t_vhdl_disconnect_unsup``, ``t_vhdl_component_*``, ``t_vhdl_*_chain*``,
``t_vhdl_mixed_array_op_bad``, ``t_vhdl_char_ctx_bad``, ``t_vhdl_case_*_bad``,
``t_vhdl_decl_*``, ``t_vhdl_label_*``, ``t_vhdl_severity_bad``,
``t_vhdl_pkg_body_orphan_bad``, ``t_vhdl_subp_dup_bad``,
``t_vhdl_entity_passive_bad``, ``t_vhdl_int_overflow_bad``,
``t_vhdl_attr_length_bad``, ``t_vhdl_shift_count_bad``,
``t_vhdl_arch_qual``, ``t_vhdl_array_idx_qual``, ``t_vhdl_inst_nomap``,
``t_vhdl_use_op``, ``t_vhdl_int_type`` and ``t_vhdl_top_case``). WP10 keeps
the null-array extension and updates the subset tier.

Two tier rows also disagree with the implementation in the other direction: a
general ``wait until <condition>`` and ``signal``-class subprogram parameters
are accepted although ``vhdl_synth_subset.md`` marks them deferred. A probe of
a signal ``out`` parameter matched VHDL's delta scheduling. If these stay,
their tier rows move to ``S``; otherwise they become refusals.

Remaining feature gaps
======================

- ``std_logic`` resolution between strong tristate drivers is not modelled.
  Conflicting enabled values resolve to 1 rather than ``'X'``. User-defined
  resolution functions remain out of scope.
- The driver count is conservative for a dynamic index: a write such as
  ``mem(addr) <= d`` is taken to cover the whole signal, so two processes writing
  through non-static indices to one unresolved memory are reported even when
  their elements would never collide. The resolved check now recurses through
  records and arrays, and scalar ``std_logic`` keeps its name, so the scalar,
  record and custom-array false positives are closed (``t_vhdl_logic_resolved``).
  ``std_logic_vector`` still escapes the frontend's unresolved set and reaches
  the middle end as multiple drivers, which emits ``MULTIDRIVEN``.
- Arithmetic and comparison under ``use ieee.std_logic_signed.all`` are
  refused with a source-located ``Unsupported: VHDL ...`` error;
  ``std_logic_unsigned`` is modeled.
- ``report`` with a non-literal message executes with fallback text rather than
  the computed message; its severity, including ``failure``, is honoured.
- PSL properties are accepted and skipped, so they never fire.
- ``while`` and bare loops in processes are refused; dynamic-loop
  ``exit``/``next`` is limited to function folding.
- ``case generate`` and matching ``case`` (``case?``), both legal VHDL-2008,
  are refused; ``case?`` surfaces as the generic parse error "expected an
  expression, got '?'". Conditional expressions, a VHDL-2019 feature, are
  likewise unsupported.
- A resolution indication such as ``signal s : resolve bit;`` is refused by a
  generic parser error, not by a named diagnostic.
- Enumeration range subtypes (``subtype e2 is e range a1 to a2;``) and the
  ``'base``, ``'subtype``, ``'element`` and ``'simple_name`` attributes are
  refused. ``'range``, ``'length``, ``'left``/``'right``, ``'high``/``'low``,
  ``'pos``/``'val``, ``'succ``/``'pred``, ``'image`` and ``'event`` are modeled.
- Timing: nonzero ``after`` inside a process, multiple waits, and
  non-clock ``wait on`` are refused.
- Libraries are not a namespace: a ``use`` clause's library name is recorded
  but never compared, and every design unit is keyed by its folded name only.
  A second package of one name is now reported instead of silently replacing
  the first (``t_vhdl_pkg_dup_bad``), so a design that needs two libraries with
  one package name cannot be analyzed at all. There is no VHDL ``-y``/``-v``
  autoload either; all design files must be given explicitly.
- A file read at elaboration is not registered as a build dependency, so a
  change to it does not trigger a re-verilation.
- Fixed-point and floating-point packages, access and protected types, and
  runtime file I/O are out of scope.
- VHDL-2019 features (interface views, conditional expressions, 64-bit
  integer) are not implemented.
- A VHDL design cannot instantiate a SystemVerilog module: a VHDL instance
  binds only to entities analyzed with it. The other direction works, both in
  one invocation and through the :vlopt:`--lib-create` library boundary that
  :ref:`vhdl_usage` describes.

Implementation quality
======================

The layering is real and is the frontend's main strength. A hand-written lexer,
a recursive-descent parser into its own tree, and a lowering pass that emits the
committed V3 AST keep the parser free of Verilator infrastructure. Unsupported
syntax is refused at parse time, diagnostics carry source locations, and no V3
node, pass or runtime primitive exists only for VHDL.

The lowering is where the cost sits. Measured on the current tree:

============================================  =====================================
Measure                                       Value
============================================  =====================================
Frontend source                               19,680 lines in 11 files
``V3VhdlNativeLower.cpp``                     14,273 lines (73% of the frontend)
``Lowering`` class declaration                1,119 lines, one class
``Lowering`` member functions                 176; 21 are over 150 lines
Largest functions                             705, 392, 374, 317, 314, 307 lines
Conditions and case labels in the lowering    2,260 and 132
Shared refusal-helper sites                   238 (233 lowering, 2 lexer, 1 parser)
Data members in ``Lowering``                  66, most of them STL containers
============================================  =====================================

The consequences are structural rather than cosmetic:

- Analysis and emission are one pass. There is no typed intermediate
  representation and no symbol table: names are resolved through string-keyed
  maps, a type is a flat ``VType`` value carrying a textual ``typeName``, and
  ``boolean`` shares the ``LOGIC`` kind with ``std_logic``. Each construct's
  typing rules are hand-written in the function that creates its AST; that is
  why type conformance had to be threaded through six separate paths instead
  of living in one semantic pass.
- Scope is a flat snapshot, not a chain. Entering a process or inlining a
  subprogram calls ``saveScope``/``restoreScope``, which copy eleven
  containers, including the whole signal map. That makes process lowering
  quadratic by construction. A local probe, with no benchmark script shipped,
  measured 0.05 s at 250 processes growing to 4.70 s at 8000; the trend is
  structural, but the absolute numbers are not reproducible from a checkout.
- Arrays and records are packed into one wide ``AstBasicDType`` vector, not an
  unpacked array or struct. This keeps slicing uniform and makes a 2^16-element
  memory cheap to lint, but an element access becomes a bit select, a dynamic
  index becomes a variable part-select, and the model has no per-element V3
  variable or type. Model and compile cost grow with the total packed width,
  and the middle end treats an element as a bit range, not as an independent
  signal.
- Refusals use one shared helper with a source location and the repository's
  ``%Error: ... Unsupported: VHDL ...`` form (238 sites: 233 in the lowering,
  2 in the lexer, 1 in the parser), so a log can tell a deliberate subset
  refusal from a hard error. Each message is bespoke, so coverage is still
  audited mechanically by the test goldens.
- Parser recovery is minimal. The first parse error in a file stops that file
  (``m_failed``), so a user gets one syntax diagnostic per file. Lowering
  errors do accumulate, so this is a parser limitation, not a frontend-wide
  one.
- The pre-width V3 contract is enforced by ``V3VhdlLink`` only when
  ``--debug-check`` is on, and it runs inside ``readFiles`` before ``V3Width``.
  The regression driver passes it (all 323 tests run that way), but the corpus
  sweep and an ordinary user run do not; the corpus leaks above are what
  escapes that check.
- Citation hygiene needs attention: 21 comments cite IEEE 1800-2023 for rules
  that are VHDL rules. Examples are the ``real`` constant comment, the
  array-index rule in ``registerType``, the subprogram-overload rule in
  ``resolveCall``, the external-name and configuration syntax comments in the
  parser, and the waveform rule for ``after``. Those rules are in IEEE
  1076-2008; the wrong reference makes the code harder to review and hides
  which clause a check implements.

Inout and tristate
==================

Verilator converts simple tristate structures into a two-state form, as
described in :doc:`../guide/languages`. An inout driver of the shape

.. code-block:: sv

   inout driver;
   wire driver = (enable) ? output_value : 1'bz;

becomes an input plus two outputs:

.. code-block:: sv

   input driver;        // Value driven in from external drivers
   output driver__en;   // True when this module drives
   output driver__out;  // Value this module drives

The VHDL frontend emits this shape:

- ``inout`` ports, scalar and packed vector, at the top level and on instances;
- ``'Z'`` is preserved in a logic constant. A ``'Z'`` that reaches a
  non-tristate destination still resolves to 0;
- a concurrent conditional assignment such as
  ``bus <= data when oe = '1' else 'Z';`` is lowered to the conditional driver
  form ``V3Tristate`` recognizes. A selected assignment with a high-impedance
  value is rewritten to the equivalent comparison chain, including a ``'Z'``
  produced by a folded function;
- a process may drive an ``inout`` port; the procedural driver is converted in
  the same way.

At a non-top instance the port becomes an input plus ``__en``/``__out``
connected to the parent signal, and reads of the signal inside the parent see
the resolved value. At the top-level C++ interface:

- without :vlopt:`--pins-inout-enables`, the port is a single inout field and
  the model holds one two-state value. The testbench writes the externally
  resolved value to that field; reads inside the design see that value. There
  is no IEEE 1164 resolution and no guaranteed OR of an external and an
  internal driver. Use the enabled interface below when another driver exists.
- with :vlopt:`--pins-inout-enables`, the port is an input and the driver is
  exposed as ``name__en`` and ``name__out``. The testbench supplies the
  externally resolved value. Use this interface when external drivers exist.

The limits are explicit. ``V3Tristate`` combines output enables; it does not
implement IEEE 1164 resolution. Two enabled drivers of different values
resolve to 1 in the two-state model, where VHDL produces ``'X'``. User-defined
resolution functions remain out of scope.

The feature is pinned by ``t_vhdl_inout`` (top level, conditional and selected
drivers, scalar and vector, ``__en``/``__out``), ``t_vhdl_inout_bus`` (two
instances on one resolved signal), ``t_vhdl_inout_proc`` (a clocked process
driver), and ``t_vhdl_logic_z`` (``'Z'`` reaching non-tristate destinations).

Verification gaps
=================

- The corpus is lint-only, and the regression suite pins expected values by
  hand. There is no automated differential simulation harness in the
  repository, so value differences must be found by targeted tests.
- The VHDL suite has no dedicated CI job; it must be run explicitly. It has
  323 drivers (74 negative, 75 diagnostic goldens); all pass.
- All VHDL test scripts are committed mode 100755, so the documented direct
  invocation ``test_regress/t/t_<name>.py`` works; ``driver.py`` also runs
  them through ``runpy``.
- The negative tests cover assignment, initial value, constant, return,
  single-candidate call, generic, conversion and operator conformance in
  ``t_vhdl_type_conf_bad``. The new tests add ``t_vhdl_intrinsic_bad``
  (intrinsic operands), ``t_vhdl_rel_array_bad`` (unrelated arrays),
  ``t_vhdl_case_metavalue_unsup``, ``t_vhdl_bit_inout_unsup``,
  ``t_vhdl_pkg_dup_bad`` and ``t_vhdl_driver_bad``'s ``std_ulogic`` case.
  Positive tests pin ``t_vhdl_report_finish``, ``t_vhdl_case_int_subtype``,
  ``t_vhdl_null_assign`` and ``t_vhdl_logic_resolved``. The remaining literal
  and array-conversion corners are untested.
- Zero-width objects in positions other than a whole-array assignment, and the
  ``BLKANDNBLK``/``ASSIGNIN`` rejects, still have no regression test; they need
  either a reduced reproducer or the external corpus.
- The external corpus and ``third_party`` trees are not part of this
  repository, so the corpus sweep and the neorv32 allocation run cannot be
  reproduced from a checkout. The 323-test suite and the probes in this page
  are the reproducible evidence.
- There is no scale or performance test. The process-lowering quadratic in
  `Implementation quality`_ is unguarded, so a regression that copies one more
  container per scope would not be noticed.
- The regression driver passes ``--debug-check``, but the corpus runner and
  ordinary runs do not. Running the corpus with the same contract check would
  move the middle-end leaks to the frontend's own diagnostics.
- Before the GHDL-XML frontend was retired, three tests did not request a
  frontend selector and therefore exercised the GHDL path even in a native run.
  Retiring the selector exposed the difference: one of them,
  ``t_vhdl_record_ind``, relied on unconstrained record specialization from
  element-only actuals, which the in-tree frontend did not support. The
  constrained shape is the positive test, and the element-only shape is now
  value-checked by ``t_vhdl_record_uncon``.
- Removing the external frontend halved the maintenance surface; the remaining
  single frontend still needs its own analyzer-grade tests, not only
  acceptance tests.

______
Order
______

1. Close the remaining middle-end leaks: zero-width objects outside a
   whole-array assignment, and the ``BLKANDNBLK`` and ``ASSIGNIN`` rejects,
   with a reduced regression test for each. Check the pre-width contract
   unconditionally rather than only under ``--debug-check``.
2. Add a differential simulation harness and a CI job, treat lint-only corpus
   counts as breadth, and add a scale test for the process path.
3. Implement ``std_logic_signed`` arithmetic and comparison, ``case generate``,
   matching ``case``, enumeration range subtypes, resolution indications and
   the missing type attributes. Tighten array-conversion conformance. The
   remaining value differences are the documented two-state model limits.
4. Revisit the large-memory and neorv32 wrapper allocation failures.
