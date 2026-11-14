.. DESCRIPTION: Verilator: native VHDL frontend journal
   SPDX-FileCopyrightText: 2026-2026 Hannes Klas
   SPDX-License-Identifier: CC0-1.0

##############################
Native VHDL frontend journal
##############################

A working log of the effort to close the gaps the native VHDL frontend
(``--vhdl-frontend native``) shows on real designs. The findings themselves,
and the test that pins each one, are in :doc:`findings`; the structure of the
frontend is in :doc:`architecture`. This file records how the work went,
including the attempts that were reverted.

______
Method
______

- A finding is closed by a frontend-only change. No AST node, pass or runtime
  primitive was added to the V3 middle end; where a fix would need one, the
  finding is recorded as blocked instead.
- Every fix carries a reproducer that fails without it, and the value is
  confirmed with an independent simulator before the test is written.
- Two simulators are used as ground truth: GHDL, and nvc (``nvc --std=2008``)
  when GHDL and the frontend disagree. nvc has settled two arguments: the value
  of a toggle flip-flop sampled at 22 ns, and the edge count in a clocked
  testbench.
- The corpus is the regression target: lints for poc-divider, hdl-modules-fifo,
  neotrng, lfsr, ring-buffer, spi-single-cs, spi-fpga-slave and spi-fpga-master,
  runs for fifo (153 checks), ring-buffer, neotrng and lfsr, and cycle traces
  for neotrng and lfsr.

_______________________________
Deferred types and overload selection
_______________________________

The first blocker was ordering: a type whose range names a constant declared
later in the same part was resolved before the constant existed. Type
registration was deferred per part and retried, and subprogram selection became
type-aware: candidates are filtered by what the actuals are, but only to choose
among several, never to validate.

Two decisions from that session are worth keeping. A candidate is excluded by
type only when more than one declaration of the same name and arity is visible;
if every candidate is refused, the explicit diagnostic is reported instead. And
the category of an actual comes from its literal kind, the type of a
designator, or the constant tables, not from lowering it, so the filter cannot
depend on lowering succeeding.

________________________________
Value tests, and one that was not a bug
________________________________

Six findings closed together: package ``real`` constants, slices whose range
opposes the array's direction, aliases whose mark reads an attribute, aggregates
as operands, attributes with arguments, and predefined unconstrained array
elements.

N20 is the instructive one. The frontend accepted a literal-range slice whose
range opposed the array's direction, and the first instinct was to refuse it. It
is a deliberate extension that a test already pinned, so the fix was narrowed to
the form that is actually wrong -- a range designator that is a declared subtype
-- and the extension stayed.

N30 needed no code at all. ``real`` is modelled as a ``double``, and IEEE 1800
leaves the precision implementation-defined, so a value GHDL rounds differently is
not a divergence to fix. The test was changed to a case both frontends agree on,
and the divergence is documented.

_______________________________
A measurement that closed a finding
_______________________________

N32 recorded an 87 GB allocation from a pure call whose actual is not static. The
recorded shape no longer reproduces: under a 2 GB ``ulimit`` the reproducer
reports the unsupported form and exits. Both loop entry points already cap the
unroll, and the statement evaluator refuses forms it cannot evaluate. The finding
was closed by pinning the refusal, not by writing a guard for a path that cannot
be reached.

_____________________________
Package specification constants
_____________________________

N29 took two attempts. The first added a deferral in ``emitConstant`` keyed on
``Expr::CALL`` and did nothing: a name with arguments parses as an *indexed* name
and only becomes a call once the prefix is known to name a subprogram. The second
consults the prefix's name as well, and the deferral works -- the
specification's pass records the constant, the body's pass completes it once the
subprogram bodies are registered, and the value is published into the package.

_____________
A runtime loop
_____________

A process ``for`` loop whose range does not fold was refused because the
unroller kept the index as a compile-time constant. Such a loop now becomes an
``AstLoop`` over a fresh integer variable that the body reads and writes through
the scope, which needed no new AST node.

The value check was corrected by GHDL: a process variable keeps its value between
process runs, so the second sum starts from the first one's total. The first
expectation, that each run starts from zero, was wrong.

_______________
A configuration
_______________

A ``configuration`` design unit was refused outright. It is now parsed and
checked against the architecture it configures. The check needed one fact the AST
did not have: whether an instantiation names an entity or a component. The parser
already knew -- ``parseInstance`` takes a ``direct`` flag it ignored -- so the
flag is recorded and the check follows.

A configuration that rebinds a component is refused rather than silently
ignored, because ignoring it would produce different hardware than the source
describes.

___________
Testbenches
___________

N37 came in five commits and one investigation.

``report`` was parsed and dropped, and ``std.env.finish`` lowered to nothing.
Those two are small, and together with a ``wait for`` they make a testbench that
prints, waits and ends its own run.

The segfault took the longest. A timed process lowered to ``AstLoop`` crashed the
frontend on the smallest case, and it did not reproduce under ``gdb``. The wrapper
was the reason: it appends ``--vhdl-frontend native``, and a direct run without
that flag had been silently using another frontend. With the flag, ``gdb`` gave a
stack in ``AstNode::addNext`` called from ``lowerWaitProcess`` -- an empty
statement list before the wait, which ``addNext`` requires to be non-null.

The concurrent form exposed a real semantic error that only the comparison
caught. ``clk <= not clk after 5 ns`` was first lowered as a nonblocking assign
after the delay, which toggles at 0, 5, 10 ... and reported the clock high at
22 ns. GHDL and nvc both say low. Waiting before the drive puts the edges at 5,
10, 15 and 20 ns and agrees with both.

Two smaller facts from the same work: a process with no sensitivity list and no
wait is refused, so a single-shot testbench ends with a bare ``wait;``, and a
timed body needs ``AstInitial``, not the static form.

______________________
Two reverted attempts
______________________

N15 (a second top from a specialized child) was attempted twice. The first
guessed reachability from the frontend's per-entity counters; the second
recorded the module every cell is built for, which cannot miss a path. Both
removed the warning on the reproducer and both broke the same 12 tests, the
second with hard faults. The conclusion is that an uninstantiated module is not
droppable here: its variables and types can still be referenced, and dropping it
leaves dangling pointers. The frontend-only lever is therefore not a deletion but
making the module not a top, and ``isTop()`` is ``level() == 1``, set by
``V3LinkCells``.
Both attempts were reverted rather than left in the tree, and the reproducer is
kept as ``test_regress/t/t_vhdl_spec_top_repro.vhdl`` so the next attempt starts
from a known case.

_______
Commits
_______

.. list-table::
   :header-rows: 1
   :widths: 14 86

   * - commit
     - change
   * - ``bf72849``
     - Defer native type registration and select overloads by argument type
   * - ``a42be89``
     - Support real package constants, derived aggregate types and IEEE vector
       elements
   * - ``23d9990``
     - Refuse a call in a constant whose actual is not static
   * - ``4db7a87``
     - Complete a package specification constant from the body's subprogram
   * - ``2dbc12e``
     - Run a process for loop whose range is not static
   * - ``63cdd33``
     - Parse a configuration and check what it binds
   * - ``b80509c``
     - Report from a VHDL process and end the simulation
   * - ``7e3fe94``
     - Wait for a delay in a VHDL process
   * - ``e162c1e``
     - Repeat a VHDL process after a delay
   * - ``50c21da``
     - Drive a VHDL signal from a delayed concurrent assignment
   * - ``7a292ab``
     - Report the edge count the testbench actually sees