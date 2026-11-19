.. DESCRIPTION: Verilator: native VHDL frontend coverage assessment
   SPDX-FileCopyrightText: 2026-2026 Hannes Klas
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
a construct that is accepted and lowered with a different meaning. Several of
those silent divergences are listed below; they are the work that must land
before the frontend can be trusted on arbitrary legal VHDL.

The in-tree frontend is the only VHDL frontend. GHDL and nvc are used as
reference simulators in tests, not as build-time frontends.

__________________
Measured coverage
__________________

The regression suite contains 239 VHDL tests; all pass. The GHDL ``synth``
testsuite corpus is used as a breadth check with ``--lint-only`` on 1139
case/top pairs (measured before the conformance review added its diagnostics,
which are stricter than the sweep's baseline; the counts below have not been
re-measured since):

========================================  =====
Result                                    Count
========================================  =====
PASS (accepted and lowered)                 880
UNSUPPORTED (clean diagnostic)              135
ERROR                                        78
EXPECTED_FAIL                                33
NO_TOP                                       13
========================================  =====

Acceptance is not correctness: the corpus is lint-only, and a design counts as
PASS when the frontend accepts it, not when its simulation matches VHDL. The
value-checked evidence is the 239 regression tests, 20 ported corpus designs
whose results are compared against GHDL, and the specific probes in this
assessment.

Of the 78 corpus errors, 40 are "entity not found" (mostly corpus-runner
selection artifacts), 4 are entities without an architecture, 7 are missing
standard functions, and none are internal faults. A sweep of 18 real designs with
whole-design linting reports 143 refusals in 39 files; 54 of those need the
whole design to answer. The neorv32 core (``rtl/core/*.vhd``, top
``neorv32_top``) lints clean; the whole ``rtl/`` tree with its stock Verilog
wrapper still aborts with ``std::bad_alloc`` under an 8 GiB limit.

__________________
Feature coverage
__________________

===========================================  =========  =====================================
Area                                         Status      Notes
===========================================  =========  =====================================
Entities, architectures, packages            Supported  Multiple architectures: last analyzed
                                                         wins; context declarations are expanded
Components and direct instantiation          Supported  Configurations are validated, not
                                                         applied
Ports, generics, generic types               Supported  Unconstrained ports specialize per
                                                         instance; inout ports use V3Tristate
Scalar, enum, record, 1-D array types        Supported  Includes arrays of arrays; multi-
                                                         dimensional ports excluded
Static real and physical types               Supported  Elaboration-time constants only
Expressions and aggregates                   Supported  Two-state model limits below
Sequential statements and subprograms        Partial    Procedures/functions inlined; dynamic
                                                         loops, impure functions, signal formals
                                                         limited
Concurrent statements and generate           Supported  Conditional assignment with no final
                                                         ``else`` is a latch holding the target
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
Mixed VHDL/SystemVerilog sources             Excluded   One language per invocation
============================================  =========  =====================================

Where the implementation is sound
=================================

- The conformance review against IEEE 1076-2008 (findings N99..N126, all closed
  with GHDL/nvc-confirmed tests) removed the silent value divergences and added
  the analysis-time checks the frontend lacked: case and selected choice lists
  must be complete and free of duplicates, an unresolved signal may not have two
  drivers on the same bit, integer literals and constants respect their ranges,
  ``'val`` is bounds-checked, division by zero is reported, associations cannot
  mix forms, port actuals must conform, and illegal identifiers, reserved
  words and a wait in a sensitized process are refused. The findings table
  records each one with its test.

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
- Most unsupported constructs produce a source-located ``Unsupported:``
  diagnostic; 135 corpus cases are classified that way.

Semantic divergences
====================

The remaining divergences come from the two-state model: ``is_x`` is always
false, and a signal cannot hold a metavalue. ``'Z'`` is preserved for a
tristate driver and resolves to zero anywhere else. Array ordering of
metavalue constants uses the two-state order, not the IEEE 1164 enumeration
order. An object that the source neither initializes nor resets starts at 0 in
the model, including an ``integer``, where IEEE 1076-2008 6.4.2.1 gives it
``T'left``; :file:`vhdl_synth_subset.md` 5.1 documents the choice. These are
documented model limits, not silent miscompilations.

The former overload and bound-check divergences are closed:

- A typed context resolves an enumeration literal, including when two visible
  types share a literal name. Without such a context, a shared literal is an
  ambiguity error rather than the last declaration silently winning.
- Two polymorphic operands are matched by their bit-string widths and the
  expected result type. If neither operand is a sized literal, the built-in
  operator applies as before.
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
and an instance that binds only some of them fills in the rest. A
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

Internal faults
===============

No corpus case now reaches an internal fault. The former crash class was closed
by:

- an ``else``-less concurrent conditional assignment is emitted as a latch that
  holds the target when no condition holds;
- a concurrent procedure assertion is evaluated in its own ``always_comb``;
- a two-dimensional element target descends one type level per index, so its
  right-hand side is widened to the element width rather than the row width;
- an unconstrained multi-dimensional port or array conversion is reported as
  ``Unsupported:`` with its location;
- repeated aggregate initializers fold a constant value into one constant and
  chunk a run-time replication, so ``(others => (others => '0'))`` over 2^16
  elements lints in 0.05 s and 20 MiB instead of aborting.

The full neorv32 ``rtl/`` tree with its stock Verilog wrapper still aborts with
``std::bad_alloc`` under an 8 GiB limit when the wrapper's complete generic set
is specialized. The core alone remains clean.

Remaining feature gaps
======================

- ``std_logic`` resolution between strong tristate drivers is not modelled.
  Conflicting enabled values resolve to 1 rather than ``'X'``. User-defined
  resolution functions remain out of scope.
- The driver count is conservative for a dynamic index: a write such as
  ``mem(addr) <= d`` is taken to cover the whole signal, so two processes writing
  through non-static indices to one unresolved memory are reported even when
  their elements would never collide. ``std_logic`` and the arrays built from it
  are resolved, so a resolved bus is unaffected.
- Arithmetic and comparison under ``use ieee.std_logic_signed.all`` are
  refused with ``Unsupported:``; ``std_logic_unsigned`` is modeled.
- ``report`` with a non-literal message is dropped.
- PSL properties are accepted and skipped, so they never fire.
- ``while`` and bare loops in processes are refused; dynamic-loop
  ``exit``/``next`` is limited to function folding.
- Timing: nonzero ``after`` inside a process, multiple waits, and
  non-clock ``wait on`` are refused.
- Libraries: there is no VHDL ``-y``/``-v`` autoload; all design files must be
  given explicitly.
- Fixed-point and floating-point packages, access and protected types, and
  runtime file I/O are out of scope.
- VHDL-2019 features (interface views, conditional expressions, 64-bit
  integer) are not implemented.
- Mixed VHDL/SystemVerilog design sources are rejected; only one language per
  invocation is supported.

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

- without :vlopt:`--pins-inout-enables`, the port is a single inout field. The
  model combines the external value with the internal driver by OR, and reads
  inside the design see that combined value. This is the most convenient
  single-driver interface, but the testbench cannot separately observe the
  module's drive when an external driver is also present.
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
- The VHDL suite has no dedicated CI job; it must be run explicitly.
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

1. Implement ``std_logic_signed`` arithmetic and comparison. The remaining
   value differences are the documented two-state model limits.
2. Add a differential simulation harness and a CI job, and treat lint-only
   corpus counts as breadth, not correctness.
3. Revisit the large-memory and neorv32 wrapper allocation failures.
