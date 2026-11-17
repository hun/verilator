.. DESCRIPTION: Verilator: native VHDL frontend journal
   SPDX-FileCopyrightText: 2026-2026 Hannes Klas
   SPDX-License-Identifier: CC0-1.0

##############################
Native VHDL frontend journal
##############################

A working log of the effort to close the gaps the in-tree VHDL frontend shows
on real designs. The findings themselves, and the test that pins each one, are
in :doc:`findings`; the structure of the frontend is in :doc:`architecture`;
the current coverage assessment is in :doc:`review`. This file records how the
work went, including the attempts that were reverted.

.. note::

   The former GHDL-XML frontend was retired: the in-tree frontend is now the
   only VHDL frontend. Earlier entries that mention selecting or debugging the
   GHDL-XML path are historical.

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

_______________________________________
Order of lowering, and folding a call
_______________________________________

A sweep of the 20 corpora's non-testbench sources gave the worklist, and the
largest family was neo430's ``entity 'X' has no port 'Y'``, reported once per port
across its cores. The cause was ordering rather than ports at all: entities were
lowered in source order, so an architecture instantiating an entity written later
read an empty port list and built cells with no pins -- a silent mis-wiring that
the diagnostic happened to reveal. Lowering children first closed the family. The
alternative considered, declaring the ports while the modules are created, needs
the generic and type resolution that only exists once the architecture is lowered.
GHDL's analysis rejects ``label : entity work.cell`` when the entity comes later,
so the test writes a component declaration with a plain identifier, which both
simulators accept and whose values could therefore be confirmed.

A generate statement's declarative part went the same way: vendor attribute specs
and a component declared inside an ``if`` generate were declarative items the
concurrent parser had no case for. Skipping the specification forms and parsing
the component away removed a 19-error family.

Three further families fell to one idea, that a call can be worth a value. neo430
writes ``constant hi_abb_c : natural := index_size_f(io_size_c) - 1;``, and
``index_size_f`` loops ``for i in 0 to natural'high`` -- so a bound placed on the
*range* refused a loop that returns after six iterations. Moving the bound to the
iterations actually folded, which is also what a ``while`` fold needs, made the
constant foldable, its uses resolvable, and 35 slice refusals went with it.
potato's ``log2`` is a ``while`` loop, and with the loop folded its array ranges
and its ``2 ** log2(N)`` exponents became static as well. Where a question of the
form "is this static?" had been answered by walking the syntax, it now lowers the
operand and folds it; one helper already did exactly that, and the exponent path
calls it instead of repeating the fallback. Two refusals stay and both are pinned:
a ``while`` whose condition follows a signal, and a loop past 4096 folded
iterations.

The fold also exposed two cascade faults. A message was built with ``VHDL `` twice
in it, and a function whose fold had already been refused was then reported as not
returning a value on all paths. The second is suppressed once any error has been
reported, which is the idiom the rest of the file uses.

______________________
Two language surfaces
______________________

A bit-string literal may state its length, and the length is part of the value:
zero padding is free, dropping a set bit is the truncation the standard forbids.
Both simulators agree on where that line falls, and one case decided the direction
of the check -- ``4o"17"`` fits 4 bits because the leading digit's zeros may go.
The predefined units of ``time`` gained ``min`` and ``hr``, which hdl-modules
writes as ``30 min``.

Comment pragmas were the last surface. neorv32 keeps its textio console printing
inside ``-- synthesis translate_off``, and the native frontend elaborated it as
design code. The lexer now drops a region's tokens, with the pragmas nesting and
any tool prefix counting, since the corpus writes ``synopsys``, ``synthesis`` and
``pragma``. The first version matched the pragma word anywhere in a comment and
silently deleted a design whose ``-- DESCRIPTION`` line merely mentioned
``translate_off``; requiring the pragma word to end the comment is the shape the
corpus actually has, all 338 occurrences of it.

A two-dimensional array needed its levels kept: ``type T_SLM is array (natural
range <>, natural range <>) of std_logic`` retained only its outer level while
unconstrained, so ``T_SLM(PORTS - 1 downto 0, DATA_BITS - 1 downto 0)`` had
nowhere to put the second constraint. Every level survives now, and the comma
form feeds the nested constraints that the chained ``t(r1)(r2)`` already had.
GHDL refuses a name that mixes a discrete name with a discrete range across two
dimensions, so the test selects whole elements and leaves that form untested.

patmos is the sweep's largest remaining count at 99, and it is not the frontend's:
its argo wrappers import ``work.ocp`` and ``work.argo_types``, neither of which is
in the repository, and its fpu reads ``IEEE.std_logic_misc``. Both are recorded as
open (N51) rather than chased.

_______________________
Units the designer adds
_______________________

poc describes its rates as physical types: ``type FREQ is range 0 to integer'high
units Hz; kHz = 1000 Hz; MHz = 1000 kHz; end units;``, and then writes
``generic (CLOCK_FREQ : FREQ := 100 MHz)``. Neither the type nor that literal was
read, so 12 poc files stopped before their first line of code and a further 15
refusals were the parser losing the generic clause at ``MHz``.

A physical literal names its unit and not its type, which is what decides where
the value is computed: the parser cannot scale ``1`` by ``MHz``, only the type
being assigned to says whether ``1`` means one step of it or a million of them.
So the parser keeps the abstract literal and the unit name beside it, and the
scaling happens at lowering against a table of every unit name and the multiple
of its type's step unit. That table is filled from the library before any
package is elaborated: a generic default lowers before the declarations that
may own the type it names. Which identifier is a unit is the grammar's own rule:
an abstract literal is followed by an identifier nowhere else, so the 115
reserved words (IEEE 1076-2008 15.10) are excluded and everything else names a
unit. Looking at the token after the identifier was the first attempt and still
misread ``0 sec then``; the reserved-word test does not.

One case stays refused. Two physical types may share a unit name with different
multiples while neither is visible to the other, and GHDL and nvc both answer
``1 k`` there -- 1000, from the expected type -- while a frontend that does not
type a literal has no answer. It reports the gap rather than picking a factor
silently. The rest is confirmed value for value: GHDL and nvc agree on all 11
expressions of the new test, from ``2 MHz + 500 kHz`` through a 1024-step
``MEMORY`` type to a fractional ``1.5 kHz``. poc's RTL refusals went from 39 to
20, and the sweep's whole RTL count from 201 to 182.

_________________
Index ranges by level
_________________

``T'LEFT(2)`` names the second index range of a matrix, and the corpus writes
it everywhere: as a subtype's bounds, as a slice, as a loop range and inside a
constraint. The bracketed index was read at each of those sites before, and
only the first level was ever used, so a two-dimensional object quietly
answered with its outer range. One helper now resolves the level and every
consumer calls it, which is what makes ``slm'range(1)`` and ``slm'high(2)``
agree.

The constraint side needed three shapes. A constraint list
``t(v'range, n - 1 downto 0)`` is the same nested array as the chained
``t(v'range)(n - 1 downto 0)``, so both feed one list of levels. ``open``
leaves an index to the object that names the subtype: ``subtype S is T(open)(1
downto 0)`` constrains only the element, and the outer range arrives with the
signal. A designator may prefix a range attribute too, as in
``v(v'low)'range``.

One fold would not terminate. A function body that writes elements binds its
locals to values, so each write rebuilds the whole value, and a loop over a
65536-entry memory multiplied that into 90 GB before the frontend answered. The
fold is now bounded by what it copies -- a wide value costs its width per write
and the rebuilt tree grows with every write -- and reports the body instead of
exhausting the machine. poc's sortnet and stat cores parse again; the
histogram's RTL count is 175, and the record constraints
``t(open)(Field(r), ...)`` of the AXI4 packages are the next family.

The record constraints took the same idea one level down. A record with
unconstrained elements is constrained where it is used, one range per member:
``rec_t(f(3 downto 0), g(x'range))``. A member's range may be written out or
named by an attribute of another object, which is how poc's ``AddressResize``
gives a bus record a new address width, and the whole list may sit behind
``open``: ``t_arr(open)(f(r))`` constrains the element and leaves the index to
the signal. Telling that list from an index range needs the range inside the
member's own parentheses -- ``t(v'range)(v(v'low)'range)`` has none, and stays
two index ranges. poc's ``axi4stream.pkg`` lints clean on its own now, and the
other AXI packages stop at names their sibling packages declare.

``T'element`` was the last shape in the same file. A generate block in poc's
Binary-to-BCD converter declares a pipeline register as ``BCDDigits'element``,
naming an output port, and the attribute is the element type of the array that
port has. The name before it may be a type or an object, so the mark is
resolved both ways: a declared type is looked up, and anything else has to be
a designator whose type is an array. That closed the last declaration error in
the file; the converter stops at the packages' own names now.

A context declaration was the last parse error in poc's common.vhdl.
`context c is` bundles library and use clauses under a name, and the file has
nothing else in it; the declaration is skipped whole, and the units after it
parse. The helper that skips a context clause also skips attribute and
disconnect specifications, so the declaration is only looked for where the
clause really starts with `context` -- the first version read
`default clocking is ...` as one and ate the next process.

A generate condition with a label was the last thing between poc's ocrom ROMs
and a clean parse: `gen: if gInfer: (VENDOR = VENDOR_GENERIC) or ... generate`.
Neither the 1076-2008 grammar nor the obvious reading explains the colon, so the
value was settled by simulation: GHDL and nvc both take the expression *after*
the label as the condition, and both reject a label that is not a plain name.
The frontend follows them and drops the label. The ROMs then stop at a generic
with no default value and no instance to bind it, which is a sweep artifact
rather than a frontend gap.

Indexing an array a function returns was reading one bit of the returned value,
because a designator could not type a call: the value's own data type is a flat
vector. The declared return type now types it, but only where the mark names a
declared array type -- an unconstrained `std_logic_vector` return has no
element width to give, and taking one from it turned a wrong value into an
error for `t_vhdl_slice`. A conversion still selects a bit, which is what it
means.

A sweep of the whole corpus with the native frontend, run per core rather than
per file, leaves two refusals on the design alone. One is a top whose ports are
unconstrained vectors, which cannot be sized without an instance. The other
was a gate-level style assignment with a delay: `q <= d after delay`, where the
delay is a generic. Zero of it is a delta, so the assignment behaves like an
un-delayed one and the delay is dropped; anything longer inside a process
needs a scheduled update and is reported. bit-serial then stops at a textio
debug procedure, which is not synthesizable.

The per-core sweep turned up one crash: indexing an array a function returns,
where the element is an unconstrained vector. The element has no width, so the
select was built with width zero and the emitter faulted. The index is reported
now. A function returning a plain `std_logic_vector` still selects one bit,
which is what its type says, so the rule is the element width and not the
return type.

The timing comparison turned up a testbench bug rather than a frontend one:
`assert false report "x" severity failure;` did not end the run, so a VHDL
testbench had no way to stop. The parser had been dropping both the message
and the severity and handing the statement to the report path, which printed
nothing and continued. The statement now keeps all three: note, warning and
error report and carry on, and failure reports and finishes. The concurrent
form is still ignored, as it was before.

Five more RTL shapes fell, all found by linting whole cores instead of files.
An entity may state its own assertions: f32c's DDR2 input writes its generic checks
there, and the statement part is now parsed, emitted into the module, and shares the
assertion body the sequential form uses. A case in a folded function body that
covers every value of its selector no longer needs an `others` arm -- a selector
that matches nothing leaves the values the body had before the case. A selected
assignment's choice may be an expression of constants, as rpu's ALU writes it, and
is folded before it is compared. A process may declare an alias, as microwatt's
sim_jtag does. And an instantiation may name its entity with a selected name:
bit-serial's UART instantiates `work.uart_pkg.uart_fifo`, and the lookahead that
tells an instantiation from a procedure call only looked one token ahead.

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
   * - ``58a0fe0``
     - Lower a VHDL entity after the entities that instantiate it
   * - ``0252dad``
     - Accept a component and attribute specs in a VHDL generate block
   * - ``62a18dd``
     - Accept every predefined VHDL time unit
   * - ``6ff30f9``
     - Keep every index level of a VHDL array type
   * - ``9e974b8``
     - Fold a VHDL function call that sets a slice bound or an exponent
   * - ``d8802b7``
     - Model the length a VHDL bit-string literal states
   * - ``39435c7``
     - Skip a VHDL comment pragma translate_off region
   * - ``bf07aa7``
     - Support a VHDL physical type with user-declared units
   * - ``08da791``
     - Support a VHDL attribute naming one index range
   * - ``177c45d``
     - Support a VHDL index constraint list and bound a folded body
   * - ``e5e3fc6``
     - Support a VHDL record constraint on a nested element
   * - ``3289c0b``
     - Support a VHDL 'element type mark
   * - ``6de3383``
     - Skip a VHDL context declaration
   * - ``12d75c8``
     - Accept a VHDL generate condition with a label
   * - ``13e5936``
     - Select an element of an array a VHDL function returns
   * - ``9d69226``
     - Accept a VHDL signal assignment with a zero delay
   * - ``ff84036``
     - Report an index of an array whose element has no width
   * - ``a0a00b7``
     - End a VHDL run on an assertion with severity failure
   * - ``154fa50``
     - Accept a VHDL entity statement part
   * - ``948ea8a``
     - Fold a VHDL case that leaves no value on some path
   * - ``d23a826``
     - Fold a VHDL choice that is an expression of constants
   * - ``ccab6d4``
     - Accept a VHDL alias in a process declarative part
   * - ``bc37b5f``
     - Accept a selected entity name in a VHDL instantiation
   * - ``ee3df9e``
     - Support conversions to the predefined integer subtypes
   * - ``3bbe87a``
     - Support the std_logic_1164 X01 conversions
   * - ``5307195``
     - Refuse an inout port where it is declared
   * - ``643e55e``
     - Resolve signal aliases, and width unconstrained vector constants
   * - ``7258f2f``
     - Fold a constant expression that has real operands
   * - ``e13eccd``
     - Pass a VHDL string generic on to a sub-instance
   * - ``fc94582``
     - Read a std.textio file into a constant at elaboration
   * - ``5855816``
     - Fill a record aggregate from an others choice
   * - ``e16c965``
     - Complete a deferred package constant in its own unit
   * - ``3ea1733``
     - Accept a discrete subtype as a range, and std_match
   * - ``51f05c0``
     - Take an inlined slice write's offset inside the local, not its actual
   * - ``e1c95a2``
     - Fold a call where an aggregate bound or conversion width is expected
   * - ``d8fda96``
     - Fold a static condition before lowering a callee body
   * - ``9f9b04b``
     - Fold images and string constants, and drop unused entities
   * - ``d3250ea``
     - Allow a function to be called inside its own arguments
   * - ``539a6b5``
     - Emit the asynchronous-reset idiom as an edge-triggered flip-flop
   * - ``facd57d``
     - Report a port that an instance associates more than once
   * - ``0ae517f``
     - Support VHDL inout ports and tristate drivers
   * - ``1050f2e``
     - Fix silent VHDL semantic divergences
   * - ``8e02b16``
     - Expand VHDL context declarations
   * - ``86b7c43``
     - Resolve typed VHDL enum literals and array semantics
