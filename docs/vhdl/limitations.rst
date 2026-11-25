.. DESCRIPTION: Verilator: VHDL supported subset and known divergences
   SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
   SPDX-License-Identifier: CC0-1.0

=============================================
VHDL supported subset and known divergences
=============================================

Verilator reads VHDL-2008 directly and lowers it to the same internal
representation as SystemVerilog. It accepts what that representation and the
runtime already express, not only what a synthesis tool accepts. It is not a VHDL simulator: the
runtime model is two-state, and constructs outside the supported subset are
refused with a diagnostic rather than silently ignored. See
:doc:`usage` for the command-line flow.

Supported subset
----------------

The subset covers entities and architectures, packages (including generic
package instantiations and deferred constants), components and direct entity
instantiation, constant and type generics, logic and numeric types, records,
one-dimensional arrays and arrays of arrays, processes and concurrent
assignments, generate statements, static loops, pure functions and procedures,
``std_logic_1164``, ``numeric_std``, the ``sfixed`` and ``ufixed`` types of
``fixed_pkg`` and the standard attributes.

``fixed_pkg`` supports ``sfixed`` and ``ufixed`` with descending ranges: ``+``, ``-``
and ``*`` with the package's result bounds, the comparisons, unary ``-`` and ``abs``,
``resize`` with saturate or wrap and round or truncate (the package defaults are
``fixed_saturate`` and ``fixed_round``), ``to_sfixed``, ``to_ufixed``, ``to_slv``,
``to_signed``, ``to_unsigned`` and ``to_integer``, and an integer or a real literal beside a
fixed-point operand. Division, ``mod``, ``rem``, shifts, ``to_real`` and arrays of
fixed-point elements are refused. The results of these operations were compared with
GHDL on every operand pair of small ranges.

A ``real`` variable or signal is a double: ``+``, ``-``, ``*``, ``/``, unary ``-``, the six
comparisons, and ``real`` of an integer. ``integer`` of a real rounds to the nearest integer,
halfway cases away from zero. A real subtype's range is not checked. ``uniform`` of
``ieee.math_real`` follows the IEEE 1076.2 generator, the same sequence as GHDL. A constant
array or record with real elements or fields is folded to its bit patterns.

The attributes ``'pos``, ``'val``, ``'succ``, ``'pred``, ``'leftof`` and ``'rightof`` apply to
integer types, and all but the last two to enumeration types. ``'value`` and ``'image`` of a
run-time value are refused.

An external name, ``<< signal dut.accum : std_logic_vector(7 downto 0) >>``, reads an
object of an instance. The path starts at this unit, as ``dut.accum`` or ``.top.dut.accum``,
or climbs out of blocks with ``^``. Paths through an indexed generate, to a package object,
or up out of this unit are refused.

A configuration specification in an architecture, ``for all|others|labels : comp use entity
work.comp(arch);``, selects the architecture of a component instance. A configuration declaration
does the same for the architecture it configures and overrides a specification. The entity must
have the component's name, and a generic map, a port map, ``use configuration`` and ``use open``
are refused, as are two configurations that select different architectures for one instance.

Library clauses, ``work`` as the default library, case-insensitive name lookup,
source locations in diagnostics, and ``.vhd``/``.vhdl`` extension detection are
supported. ``--top-module`` selects the top entity.

A context clause applies to one design unit and to that entity's architectures,
as in a VHDL analyzer. The types of the IEEE packages ``std_logic_1164``,
``numeric_std``, ``numeric_bit`` and ``std_logic_arith`` are visible only where
a use clause, a use clause naming that one type, or a selected name such as
``ieee.std_logic_1164.std_logic`` makes them so; otherwise the type is refused
as not visible. A second entity in a file therefore needs its own context
clause.

Mixed-language designs are partially supported: a SystemVerilog top may
instantiate a VHDL entity in one invocation, and a VHDL hierarchy can be built
as a ``--lib-create`` library and instantiated from SystemVerilog. A VHDL design
cannot directly instantiate a SystemVerilog module; instantiate the VHDL design
from a SystemVerilog top instead. No types, packages or hierarchical references cross that boundary.

Model divergences
-----------------

Value model
~~~~~~~~~~~

``std_logic`` is collapsed at elaboration because the runtime is two-state:
``'0'``/``'L'`` become 0, ``'1'``/``'H'`` become 1, and ``'X'``/``'U'``/``'W'``/
``'-'`` become X in constants but 0 or 1 at run time. A ``'Z'`` constant is
preserved for a tristate driver and resolves to 0 elsewhere. There is no
nine-state resolution: two enabled drivers with different values resolve to 1,
where VHDL produces ``'X'``. VHDL starts an object with no initializer at
``'U'`` or ``T'left``; the model starts it at 0 by default.

Verilator's reset randomization applies as for SystemVerilog. With
:vlopt:`--x-initial` ``unique``, its default, running the model with
``+verilator+rand+reset+2`` starts vectors and ``std_logic`` signals that have
no initializer at random values, chosen by ``+verilator+seed+<value>``, which
exposes a missing reset. An object with an explicit initializer keeps it.
``integer`` objects are not randomized and always start at 0, as a
SystemVerilog ``int`` does.

Signals, variables and scheduling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Signal assignment is a non-blocking update lowered to a delayed or continuous
assignment; variable assignment is immediate. Verilator's settle scheduling
approximates VHDL delta cycles, but ordering between independent processes is
not guaranteed to match a simulator cycle for cycle. ``process(all)`` infers
sensitivity from the signals read by the lowered body; explicit sensitivity
lists are honored as written. ``postponed`` on a process or an assertion is
accepted and ignored: the statement runs with the others, not at the end of the
time step.

Integers, ranges and packing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``integer`` is 32-bit signed, with ``natural`` and ``positive`` as subtypes.
Subtype bounds are checked statically; runtime range violations are not
detected. Enumeration encoding follows declaration order. Arrays and records
are packed into one wide vector: indexing becomes a bit select and a dynamic
index becomes a variable part-select, so model and compile cost grow with total
packed width. There is no independent V3 variable or type per element.

Waveform tracing
~~~~~~~~~~~~~~~~

Tracing with :vlopt:`--trace-vcd`, :vlopt:`--trace-fst` and the other trace
formats keeps the design hierarchy and the VHDL signal names, but the types
are those of the packed model. An enumeration signal appears as its integer
encoding, in declaration order, without the literal names, in FST as well as
VCD. A record signal appears as one vector with the fields concatenated, the
first declared field in the high bits, and :vlopt:`--trace-structs` does not
split it into fields. An array is one vector in the same way.

Assertions and reports
~~~~~~~~~~~~~~~~~~~~~~

``assert`` and ``report`` syntax is accepted. Static assertions may be
evaluated during elaboration. A report always lowers to a display: a literal
message is printed, while a non-literal one falls back to the text ``report``
(an assertion falls back to ``Assertion failed``) without dropping the
statement. A report or failing assertion of ``severity failure`` stops the run
there like ``$fatal``, so the simulation exits with a non-zero status. General
simulation-control semantics are not modeled.

Timing
~~~~~~

``wait for`` is supported: a timed process lowers to an initial block that
repeats after an ``AstDelay``. A nonzero ``after`` on a concurrent signal
assignment is also supported and lowers to a timed always block. Both delays
keep their absolute VHDL time whatever :vlopt:`--timescale` says. A zero
``after`` in a process body is a delta; a nonzero one there is refused, as are
``transport``, ``inertial`` and ``reject``. A process with more than one ``wait``,
or a ``wait`` inside an ``if``, ``case`` or ``for``, lowers to one timed block that
repeats its body: ``wait for`` is a delay, ``wait on`` and ``wait until`` are an event
control, and a bare ``wait;`` suspends the process for good. This needs a timing
build such as ``--binary``. A ``wait until`` that names an edge resumes on that edge
only. A ``wait`` inside a ``while`` or bare ``loop`` is refused, as those loops are.
Rather than silently dropping a delay that would change behavior,
the frontend emits a diagnostic. Single top-level clock waits and simple timed
testbench constructs are supported by the ``--binary`` timing model.

Inout and tristate
~~~~~~~~~~~~~~~~~~

A top-level ``inout`` with :vlopt:`--pins-inout-enables` is exposed as an input
plus ``name__en`` and ``name__out``; use this when an external driver exists.
Without it the port is one two-state field: the testbench writes the externally
resolved value and reads inside the design see that value. There is no
IEEE 1164-1993 resolution and no guaranteed OR of an external and an internal
driver. ``V3Tristate`` combines output enables only.

Not supported
-------------

These constructs are refused with a diagnostic: access, protected, file and
``textio`` types; ``force``/``release``; runtime ``'value`` and string parsing;
physical objects and ``now``; real ports; arrays and records with real elements that
are variables or signals; the operators ``**`` and ``mod`` on a real; and the floating-point
package ``float_pkg``;
user-defined resolution functions; allocators; multi-dimensional
ports; ``linkage`` ports, ``disconnect`` and ``guarded`` signals; embedded
other-language source; and general timing constructs. VHDL-2019 is outside the
baseline. Zero-width arrays are supported as empty values only in whole-array
assignments, and a slice with a static null range converted by ``to_integer`` gives 0 as
numeric_std does; other uses are not modeled.

Known limitations
-----------------

- Dynamic-index driver counts and ``std_logic_vector`` ``MULTIDRIVEN``
  diagnostics are still imprecise.
- ``std_logic_signed`` and ``std_logic_unsigned`` treat a ``std_logic_vector`` as a
  number in ``+``, ``-``, ``*`` and the comparisons, and ``+`` and ``-`` also take a
  ``std_logic``. Unary ``-`` and ``abs`` on such a vector are refused.
- PSL directives are skipped rather than checked.
- Visibility is enforced for the types of the IEEE packages only. The
  subprograms of those packages are not checked, so ``to_integer`` is
  accepted without ``use ieee.numeric_std.all``, and a missing
  ``library ieee;`` clause is not reported, where a VHDL analyzer rejects both.
  A unit that calls these subprograms almost always declares a signal or port of
  the matching type, and that type is checked, so the gap shows only in a unit
  that uses the functions on ``integer`` or ``std_logic`` alone. The frontend
  resolves these subprograms by name rather than through the package, which
  makes a precise check hard.
- ``case generate``, enumeration range subtypes and
  resolution indications are not implemented, and neither are type attributes
  such as ``'image`` on an enumeration.
- Library namespace handling is simplified; library names can come from a
  libmap even for a VHDL ``work`` file in a mixed run.
- Some legal constructs are rejected as ``BLKANDNBLK`` or ``ASSIGNIN``.
- Process lowering snapshots the scope on entry; the signal map is
  copy-on-write, while the other declaration tables are copied. There is no
  scale regression test for process-heavy designs.
- Large memories and some whole-core elaborations can exhaust memory.
