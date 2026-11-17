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

The regression suite contains 191 VHDL tests; all pass. The GHDL ``synth``
testsuite corpus is used as a breadth check with ``--lint-only`` on 1139
case/top pairs:

========================================  =====
Result                                    Count
========================================  =====
PASS (accepted and lowered)                 864
UNSUPPORTED (clean diagnostic)              135
ERROR                                        91
EXPECTED_FAIL                                36
NO_TOP                                       13
========================================  =====

Acceptance is not correctness: the corpus is lint-only, and a design counts as
PASS when the frontend accepts it, not when its simulation matches VHDL. The
value-checked evidence is the 191 regression tests, 20 ported corpus designs
whose results are compared against GHDL, and the specific probes in this
assessment.

Of the 91 corpus errors, 40 are "entity not found" (mostly corpus-runner
selection artifacts), 4 are entities without an architecture, 11 are missing
standard functions, and 8 are internal faults. A sweep of 18 real designs with
whole-design linting reports 160 refusals in 44 files; 52 of those need the
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
                                                         wins; context references are skipped
Components and direct instantiation          Supported  Configurations are validated, not
                                                         applied; inout ports refused
Ports, generics, generic types               Supported  Unconstrained ports specialize per
                                                         instance; generic subprograms mostly not
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
   * - ``std_logic_vector`` comparison with unequal lengths
     - predefined array equality is false; array ordering is lexicographic
     - operands are resized and compared numerically
   * - ``bit_vector sra``/``sla``
     - vacated positions use the array's leftmost/rightmost element
       (IEEE 1076-2008 9.2.3)
     - zero fill
   * - Two enumeration types with the same literal name
     - the literal is resolved by the expected type
     - one global literal table is used; the last declaration wins
   * - User-defined infix operator, e.g. ``"+"`` on an array type
     - the overload is called
     - the built-in operator is applied instead
   * - Overloads of ``unsigned`` and ``signed``, or of two array types
     - selected by the full parameter type (IEEE 1076-2008 12.5)
     - both are the same broad category, so the first declaration wins
   * - ``use ieee.std_logic_signed.all``
     - arithmetic and comparison are signed
     - ``std_logic_vector`` operations are unsigned
   * - ``report "text" severity failure;``
     - terminates the simulation (IEEE 1076-2008 8.3)
     - prints and continues; only ``assert`` terminates
   * - ``std_logic'('X') = std_logic'('X')``
     - true (identity equality in ``std_logic_1164``)
     - false; four-state equality is used, not case equality
   * - ``context work.ctx;``
     - makes the context's declarations visible
     - the reference is skipped, so its types and packages are not imported
   * - ``'succ`` of the last enumeration literal, ``'pred`` of the first
     - a bound check fails
     - the value wraps

The two-state model makes some of these unavoidable at runtime: ``is_x`` is
always false, ``'Z'`` constants become zero, and a signal cannot hold a
metavalue. Those are documented divergences. The items in the table above are
different: they change the value of legal, entirely two-state designs and do so
without a diagnostic.

Two library caveats deserve emphasis. Unknown standard packages are treated as
intrinsics instead of being refused, which is why ``std_logic_signed`` is
accepted with unsigned semantics. And vector operators are lowered by their
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

- ``inout`` ports are refused at declaration, so bidirectional buses do not
  elaborate. This is the next planned feature; see below.
- An unconstrained record port can be specialized when a whole-record actual
  supplies the member ranges, but not when only individual record elements are
  associated (``i.x => a, i.y => b``). The frontend reports a clean
  ``Unsupported``; ``t_vhdl_record_ind_unsup`` pins it.
- Multiple drivers and user resolution functions are not modelled; the middle
  end reports a multiple-driver warning rather than VHDL resolution.
- Context references do not import their declarations.
- Concurrent ``unaffected`` is reported as an unknown identifier.
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

Next target: inout and tristate
===============================

Verilator already converts simple tristate structures into a two-state form.
As described in :doc:`../guide/languages`, an inout driver of the shape

.. code-block:: sv

   inout driver;
   wire driver = (enable) ? output_value : 1'bz;

becomes an input plus two outputs:

.. code-block:: sv

   input driver;        // Value driven in from external drivers
   output driver__en;   // True when this module drives
   output driver__out;  // Value this module drives

The VHDL equivalent is an ``inout`` port driven by a conditional assignment
such as ``bus <= 'Z' when oe = '0' else data;``. The frontend currently refuses
the port and collapses ``'Z'`` to zero, so no tristate driver reaches
``V3Tristate``. Supporting the feature needs:

- accept ``inout`` ports and emit the port variable with an inout direction;
- preserve the high-impedance value in the driver expression instead of
  mapping it to zero;
- lower conditional and selected assignments to the conditional-driver shape
  ``V3Tristate`` recognizes, including the enable expression;
- keep reads of the port inside the entity consistent with the resolved input
  plus the local driver;
- add a positive regression test with a C++ harness that drives the input and
  reads ``__en`` and ``__out``, and keep a negative test for the cases the
  conversion cannot represent.

The limits are explicit. ``V3Tristate`` combines output enables; it does not
implement nine-state resolution, so two strong conflicting drivers still do
not produce ``'X'``. That remains a documented divergence. At the top-level
C++ interface an inout port will expose ``__en``/``__out`` names, as it does
for SystemVerilog; testbenches must drive the input and read the other two.

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

1. Fix the silent divergences: the conditional assignment crash and hold
   semantics, enumeration literal scoping, operator and overload resolution,
   array comparison and ``bit_vector`` shifts, ``std_logic_signed``,
   ``report`` severity, and ``context`` references.
2. Bound repeated aggregate expansion, then revisit the large-memory and
   neorv32 allocation failures.
3. Turn the remaining internal faults into clean diagnostics and pin them with
   negative tests.
4. Add inout/tristate using the existing ``V3Tristate`` conversion.
5. Add a differential simulation harness and a CI job, and treat lint-only
   corpus counts as breadth, not correctness.
