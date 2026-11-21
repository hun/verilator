.. DESCRIPTION: Verilator: VHDL supported subset and known divergences
   SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
   SPDX-License-Identifier: CC0-1.0

=============================================
VHDL supported subset and known divergences
=============================================

Verilator reads synthesizable VHDL-2008 directly and lowers it to the same
internal representation as SystemVerilog. It is not a VHDL simulator: the
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
``std_logic_1164``, ``numeric_std`` and the standard attributes.

Library clauses, ``work`` as the default library, case-insensitive name lookup,
source locations in diagnostics, and ``.vhd``/``.vhdl`` extension detection are
supported. ``--top-module`` selects the top entity.

Mixed-language designs are partially supported: a SystemVerilog top may
instantiate a VHDL entity in one invocation, and a VHDL hierarchy can be built
as a ``--lib-create`` library and instantiated from SystemVerilog. A VHDL design
cannot directly instantiate a SystemVerilog module; use the library boundary
instead. No types, packages or hierarchical references cross that boundary.

Model divergences
-----------------

Value model
~~~~~~~~~~~

``std_logic`` is collapsed at elaboration because the runtime is two-state:
``'0'``/``'L'`` become 0, ``'1'``/``'H'`` become 1, and ``'X'``/``'U'``/``'W'``/
``'-'`` become X in constants but 0 or 1 at run time. A ``'Z'`` constant is
preserved for a tristate driver and resolves to 0 elsewhere. There is no
nine-state resolution: two enabled drivers with different values resolve to 1,
where VHDL produces ``'X'``. Objects with no initializer or reset start at 0,
including ``integer`` (VHDL would give ``T'left``).

Signals, variables and scheduling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Signal assignment is a non-blocking update lowered to a delayed or continuous
assignment; variable assignment is immediate. Verilator's settle scheduling
approximates VHDL delta cycles, but ordering between independent processes is
not guaranteed to match a simulator cycle for cycle. ``process(all)`` infers
sensitivity from the signals read by the lowered body; explicit sensitivity
lists are honored as written.

Integers, ranges and packing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``integer`` is 32-bit signed, with ``natural`` and ``positive`` as subtypes.
Subtype bounds are checked statically; runtime range violations are not
detected. Enumeration encoding follows declaration order. Arrays and records
are packed into one wide vector: indexing becomes a bit select and a dynamic
index becomes a variable part-select, so model and compile cost grow with total
packed width. There is no independent V3 variable or type per element.

Assertions and reports
~~~~~~~~~~~~~~~~~~~~~~

``assert`` and ``report`` syntax is accepted. Static assertions may be
evaluated during elaboration. A report always lowers to a display: a literal
message is printed, while a non-literal one falls back to the text ``report``
(an assertion falls back to ``Assertion failed``) without dropping the
statement. A report or assertion of ``severity failure`` ends the run at that
statement. General simulation-control semantics are not modeled.

Timing
~~~~~~

``wait for`` is supported: a timed process lowers to an initial block that
repeats after an ``AstDelay``. A nonzero ``after`` on a concurrent signal
assignment is also supported and lowers to a timed always block. Both delays
are scaled to the module's time unit. A zero ``after`` in a process body is a
delta; a nonzero one there is refused, as are ``transport``, ``inertial`` and
``reject``. Rather than silently dropping a delay that would change behavior,
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
physical objects and ``now``; real objects and fixed/floating-point packages;
user-defined resolution functions; external names; allocators; multi-dimensional
ports; ``linkage`` ports, ``disconnect`` and ``guarded`` signals; embedded
other-language source; and general timing constructs. VHDL-2019 is outside the
baseline. Zero-width arrays are supported as empty values only in whole-array
assignments; other uses are not modeled.

Known limitations
-----------------

- Dynamic-index driver counts and ``std_logic_vector`` ``MULTIDRIVEN``
  diagnostics are still imprecise.
- ``std_logic_signed`` arithmetic and comparison are not modeled.
- PSL directives are skipped rather than checked.
- ``case generate`` and matching ``case`` are not implemented.
- Library namespace handling is simplified; library names can come from a
  libmap even for a VHDL ``work`` file in a mixed run.
- Some legal constructs are rejected as ``BLKANDNBLK`` or ``ASSIGNIN``.
- Process lowering snapshots the scope on entry; the signal map is
  copy-on-write, while the other declaration tables are copied. There is no
  scale regression test for process-heavy designs.
- Large memories and some whole-core elaborations can exhaust memory.
