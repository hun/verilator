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

The regression suite contains 204 VHDL tests; all pass. The GHDL ``synth``
testsuite corpus is used as a breadth check with ``--lint-only`` on 1139
case/top pairs:

========================================  =====
Result                                    Count
========================================  =====
PASS (accepted and lowered)                 869
UNSUPPORTED (clean diagnostic)              135
ERROR                                        89
EXPECTED_FAIL                                33
NO_TOP                                       13
========================================  =====

Acceptance is not correctness: the corpus is lint-only, and a design counts as
PASS when the frontend accepts it, not when its simulation matches VHDL. The
value-checked evidence is the 204 regression tests, 20 ported corpus designs
whose results are compared against GHDL, and the specific probes in this
assessment.

Of the 89 corpus errors, 40 are "entity not found" (mostly corpus-runner
selection artifacts), 4 are entities without an architecture, 10 are missing
standard functions, and 8 are internal faults. A sweep of 18 real designs with
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
Expressions and aggregates                   Supported  With the divergences below
Sequential statements and subprograms        Partial    Procedures/functions inlined; dynamic
                                                         loops, impure functions, signal formals
                                                         limited
Concurrent statements and generate           Supported  Conditional assignment without
                                                         ``else`` is a crash, see below
``numeric_std`` and ``std_logic_1164``       Partial    Arithmetic and conversions; not every
                                                         function; see library gaps
``std_logic_arith``, ``std_logic_signed``    Partial    Conversions work; signed operators on
                                                         ``std_logic_vector`` are modelled unsigned
``math_real``                                Partial    Locally static constants only
Access, protected, file, textio runtime      Excluded   File contents can be read at
                                                         elaboration; runtime objects are refused
Timing, multiple waits, external names       Excluded   Single top-level wait and timed
                                                         testbench constructs are supported
Mixed VHDL/SystemVerilog sources             Excluded   One language per invocation
============================================  =========  =====================================

Where the implementation is sound
=================================

- Elaboration is the strongest part: generic binding and specialization,
  unconstrained ports, generate unrolling, package bodies and deferred
  constants, record and array packing, and component binding are exercised by
  the suite and by ported corpus designs.
- The clocked-process model is correct for the supported idioms: explicit
  sensitivity lists, ``rising_edge``/``falling_edge`` and ``'event`` clock
  tests, synchronous and asynchronous resets, ``wait until`` splitting,
  ``process(all)`` read inference, and time-zero activation. The asynchronous
  reset idiom is emitted as a true edge-triggered block.
- ``numeric_std`` arithmetic follows VHDL for widths, sign extension, division,
  ``mod``/``rem``, ``resize``, and the shift/rotate functions on ``unsigned``
  and ``signed``. Overflow and runtime range checks are intentionally absent;
  integer arithmetic is 32-bit.
- Slicing, indexing, ascending and descending ranges, records, and aggregate
  targets are value-tested.
- Inout and tristate: scalar and vector ports, concurrent conditional and
  selected drivers, process drivers, hierarchical connections, and the top-level
  enable/drive interface are tested. Conflicting drivers remain a documented
  two-state divergence.
- Most unsupported constructs produce a source-located ``Unsupported:``
  diagnostic; 135 corpus cases are classified that way.

Silent semantic divergences
===========================

The following constructs are legal VHDL, accepted by the frontend, and lowered
with a different result. The reference values were confirmed with GHDL.

.. list-table::
   :header-rows: 1
   :widths: 34 35 31

   * - Construct
     - VHDL behavior
     - Frontend behavior
   * - Two visible enumeration types share a literal and no expected type picks one
     - overload resolution picks the literal from the expected type
     - one global literal table is used; the last declaration wins
   * - User-defined infix operator, e.g. ``"+"`` on an array type
     - the overload is called
     - the built-in operator is applied instead
   * - Overloads of ``unsigned`` and ``signed``, or of two array types
     - selected by the full parameter type (IEEE 1076-2008 12.5)
     - both are the same broad category, so the first declaration wins
   * - ``'succ``/``'pred`` of an end literal with a non-static argument
     - a bound check fails
     - the runtime step wraps

The two-state model makes some of these unavoidable at runtime: ``is_x`` is
always false, and a signal cannot hold a metavalue. ``'Z'`` is preserved for a
tristate driver and resolves to zero anywhere else. Array ordering of
metavalue constants uses the two-state order, not the IEEE 1164 enumeration
order. Those are documented divergences. The items in the table above are
different: they change the value of legal, entirely two-state designs and do so
without a diagnostic.

A typed context now resolves enumeration literals, including two types that
share a name, and a static out-of-range ``'succ``/``'pred`` is a frontend error;
only the dynamic value still wraps. Array equality works for any element type;
ordering is lexicographic for an array of a discrete element type, and an array
whose element is not discrete is rejected because VHDL defines no ordering for
it.

Two library caveats deserve emphasis. Unknown standard packages are treated as
intrinsics instead of being refused; ``std_logic_unsigned`` matches the modeled
unsigned behavior, while an arithmetic or comparison operator under
``std_logic_signed`` is refused. And vector operators are lowered by their
symbol, not by resolving the visible overload set, so a design's own operator
functions are bypassed. Both are structural: they need either real overload
resolution or a diagnostic when the operator cannot be justified.

Crash-class gaps
================

These inputs should produce a clean ``Unsupported:`` or user error; instead the
frontend or a later pass aborts:

- A concurrent conditional assignment with no final ``else``
  (``y <= a when c1 else b when c2;``) builds a conditional expression with no
  else value and fails with an internal error. This is legal VHDL: the target
  keeps its value when no condition holds. It accounts for two corpus faults
  and is the most likely to be hit by real code.
- A 2-D array port or a 2-D array type conversion fails with "AstVar created
  with no dtype" or a generic internal fault instead of the documented
  multi-dimensional-port refusal.
- A concurrent procedure call whose body contains an assertion reaches
  ``V3Localize`` with a variable reference outside a function.
- Element-wise concurrent writes to a 2-D array reach the DFG with a
  mismatched width.
- A memory initializer of the form ``(others => (others => '0'))`` expands into
  a chain of concatenations. The cost is quadratic in the array size: 2^12
  elements lint in 0.2 s, 2^14 in 2.3 s and 1.6 GiB, and 2^16 aborts under an
  8 GiB limit. Large memory images are ordinary RTL.
- The full neorv32 ``rtl/`` tree with its Verilog wrapper hits the same
  allocation failure when the wrapper's complete generic set is specialized.

Remaining feature gaps
======================

- ``std_logic`` resolution between strong tristate drivers is not modelled.
  Conflicting enabled values resolve to 1 rather than ``'X'``. User-defined
  resolution functions remain out of scope.
- Concurrent ``unaffected`` is reported as an unknown identifier.
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
  repository, which is why the silent divergences above survive.
- The VHDL suite has no dedicated CI job; it must be run explicitly.
- Before the GHDL-XML frontend was retired, three tests did not request a
  frontend selector and therefore exercised the GHDL path even in a native run.
  Retiring the selector exposed the difference: one of them,
  ``t_vhdl_record_ind``, relied on unconstrained record specialization from
  element-only actuals, which the in-tree frontend does not support. Its
  constrained shape is now the positive test, and the unsupported shape is
  pinned by ``t_vhdl_record_ind_unsup``.
- Removing the external frontend halved the maintenance surface; the remaining
  single frontend still needs its own analyzer-grade tests, not only
  acceptance tests.

______
Order
______

1. Finish the silent divergences: the conditional assignment crash and hold
   semantics, enumeration literal scoping, operator and overload resolution,
   ``std_logic_vector`` array comparison, and ``std_logic_signed``.
2. Bound repeated aggregate expansion, then revisit the large-memory and
   neorv32 allocation failures.
3. Turn the remaining internal faults into clean diagnostics and pin them with
   negative tests.
4. Add a differential simulation harness and a CI job, and treat lint-only
   corpus counts as breadth, not correctness.
