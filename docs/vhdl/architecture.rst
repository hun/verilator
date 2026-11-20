.. DESCRIPTION: Verilator: native VHDL frontend architecture
   SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
   SPDX-License-Identifier: CC0-1.0

##################################
Native VHDL frontend architecture
##################################

The native frontend translates SystemVerilog's cousin, VHDL, directly into the
V3 AST that the rest of Verilator consumes. It is the only VHDL frontend and
runs in one process, with no intermediate files. Everything below is inside the
frontend; the middle end and the runtime library are unchanged by it, and no V3
AST node, pass or runtime primitive exists only for VHDL.

The driver is ``V3VhdlParseImp::finish()``, which calls
``V3VhdlNative::emit(rootp, files)``. From there to the netlist:

.. code-block:: text

   files --> parse --> DesignLibrary --> lowerPackages --> createModules
         --> per-entity lower --> V3 AST --> (unchanged V3 passes)

_______________
Source layout
_______________

======================================================  ============================================
File                                                    Role
======================================================  ============================================
``src/V3VhdlParse.h``, ``.cpp``                         Frontend driver: collects design files
``src/V3VhdlNative.h``, ``.cpp``                        Frontend entry: parse, elaborate, lower
``src/V3VhdlNativeAst.h``                               The frontend's own syntax tree
``src/V3VhdlNativeLexer.cpp``                           Lexer: tokens, including VHDL literals
``src/V3VhdlNativeParse.cpp``                           Parser: design units to ``DesignLibrary``
``src/V3VhdlNativeLower.h``, ``.cpp``                   Elaboration: the frontend AST to the V3 AST
======================================================  ============================================

The frontend is deliberately two trees rather than one. The parser builds a tree
that models VHDL as written -- design units, declarative parts, statements,
expressions -- and the lowering pass walks it into V3 nodes. Keeping them apart
is what lets a construct be refused with a diagnostic that names the source
before any V3 node exists, and what keeps the parser free of Verilator's
infrastructure.

__________________
The frontend's AST
__________________

``V3VhdlNativeAst.h`` declares the design units and the trees inside them.

Design units, in source order, all in one ``DesignLibrary``:

- ``EntityUnit`` -- ports, generics, and the declarative items every
  architecture of the entity sees.
- ``Architecture`` -- declarative items and concurrent statements.
- ``PackageUnit`` -- a specification or a body; ``isBody`` distinguishes them.
- ``PackageInstance`` -- ``package name is new base generic map (...)``.
- ``ConfigurationUnit`` with its ``ConfigBinding`` list -- what a configuration
  binds, checked against the architecture rather than lowered.

Statements are ``Stmt``, concurrent statements are ``Concurrent``, and both
carry an ``Expr`` tree. An expression is a tagged node: literals, names,
``PAREN``, ``UNARY``, ``BINARY``, ``INDEX``, ``CALL``, ``SLICE``, ``ATTR``,
``QUAL``, ``SELECTED``, and the three aggregate forms.

One detail matters throughout the lowering: a name with arguments parses as an
``INDEX`` whose prefix is a name, and only becomes a ``CALL`` once the prefix is
known to name a subprogram. Code that looks for a call must therefore consult
the prefix as well as a folded ``CALL`` name.

__________________
Parse
__________________

``parseFile(file, library)`` runs the lexer and the parser over one file and
appends its design units to the library. Context clauses (``library``, ``use``)
are collected per design unit, so each unit carries the packages it can see.

``parseDesignFile`` dispatches on the design-unit keyword: ``entity``,
``architecture``, ``package``, ``configuration``, plus the PSL directives that
are skipped. Every construct the frontend does not model is refused by the
lexer, parser, or lowering pass with a diagnostic naming the construct; most
refusal sites are in lowering, so most unsupported features surface during
elaboration.

The parser also carries the checks that need the source form rather than a
lowered value:

- the lexer checks the basic identifier spelling of IEEE 1076-2008 15.4.2
  (leading, trailing and doubled underscores), and a declared name may not be a
  reserved word (15.10). A reserved word that the grammar itself uses -- an
  attribute name, `others` -- stays legal, so the name reader and the
  name-taking helper are separate;
- an association list is either all positional or all named (6.5.7.2), and a
  positional `open` is an unconnected actual like the named form;
- a chain of logical operators of one precedence level needs parentheses unless
  every operator is the same associative one (9.1);
- a use clause carries the one declaration it names, if any, so the lowering can
  import that declaration instead of the whole package (12.4);
- a sensitivity item may name a signal element (a selected, indexed or sliced
  name); the element designator is its own sensitivity item, so the process
  runs on that element's events and not on another element's (11.3).

___________
Elaboration
___________

``V3VhdlNative::emit`` is the ordered part. Order is the design:

1. **Bind entities to architectures.** The last analyzed architecture of an
   entity is the default. A map from entity name to architecture is built once
   and shared.
2. **Elaborate packages.** ``lowerPackages`` fills a ``PackageTable``; a
   package body completes the constants its specification deferred, and a
   generic package is instantiated per binding.
3. **Create one module per entity.** ``createModules`` makes the base module
   before any instance is seen, so an instance can bind to any entity.
4. **Collect unconstrained record marks.** A record port whose element width
   comes from an actual needs the actual, so the marks are recorded before
   lowering.
5. **Lower each entity** into its module, then remove what cannot be
   elaborated: an entity whose generics have no defaults and no binding, or
   whose unconstrained ports no instance fixes, is dropped when its
   specializations exist, and reported otherwise.

The pass is one ``Lowering`` object per module, sharing a ``Design`` context:
the entity and architecture maps, the specialization cache, and the counters
that name specialized modules.

_________
Lowering
_________

``Lowering::emit`` lowers an entity and its architecture into an ``AstModule``.
Its state is what makes VHDL's declarative parts work:

======================================================  ============================================
Member                                                  Meaning
======================================================  ============================================
``m_scope``                                             Objects visible now, by name
``m_types``                                             Types visible now
``m_consts``, ``m_preConsts``                           Integer constants, folded and pre-scanned
``m_realConsts``, ``m_enumConsts``                      Real and enumeration constants
``m_subprograms``                                       Subprograms visible now, by name and frame
``m_moduleScope``                                       The scope an inlined body evaluates against
``m_loops``                                             Active loops, for ``exit`` and ``next``
``m_initial``                                           Lowering a time-zero prefix
``m_varVals``                                           Inline bindings of a called subprogram
======================================================  ============================================

**Names.** ``lowerExpr`` resolves a ``NAME`` against the inline bindings, the
integer, real and enumeration constants, and then ``m_scope``. Anything else is
an alias, a subprogram call with no parentheses, or an unknown identifier. The
same order is used by the type resolver, so a name means one thing everywhere.

**Types.** ``registerTypes`` walks a declarative part and models each type as a
``VType``: ``LOGIC``, ``BIT``, ``BOOLEAN``, ``INT``, ``VECTOR``, ``ARRAY``,
``RECORD``, ``ENUM``, ``REAL``. Registration is deferred per part when a type's
range names a constant the part declares later, and retried when that constant
is elaborated. ``resolveType`` retries a first use the same way, so a type is
never resolved against a half-built part.

**Constants.** A constant is folded at elaboration time. Integer constants go
through ``evalInt``, reals through ``evalReal``, and a constant declared in a
package specification but initialized by a subprogram whose body is in the
package body is deferred to the body's pass.

**Subprograms.** ``registerSubprograms`` records each declaration with the frame
it belongs to; ``visibleSubprograms`` returns the innermost frame's
declarations, so a nested declaration hides an outer one. A pure function with a
body is inlined by ``pushPureCall``: its formals are bound to the actuals and
the body is folded by the statement evaluator, which is how a function call ends
up in a constant, a range or a generate condition. Selection among several
declarations of the same name is by type, and only to choose: a candidate is
excluded only when more than one is visible, so a single candidate is never
validated by the filter.

**Statements.** ``lowerStmt`` dispatches to the lowering for each form. A
process with a sensitivity list becomes a V3 always block; a process that
suspends on a wait is split at the wait, with the statements before it emitted
as a time-zero prefix and the rest as the repeated body. A bare ``wait;``
reached at run time sets a flag that gates every later activation, so the
process suspends for good; a wait of any other form inside a lowered body is
refused, since a sensitized process cannot contain one. A ``for`` loop whose
range folds is unrolled with the index as a compile-time constant; a range that
does not fold becomes a runtime loop over a fresh integer variable that the body
reads and writes through the scope, with the bound captured into a temporary at
entry so the trip count cannot change. A ``wait for`` or an ``after`` delay
becomes an ``AstDelay`` scaled to the module's time unit, defaulting to one
picosecond when the design has no timescale.

**Choice lists.** Every case and selected-assignment choice list is validated as
it lowers: a value covered twice (an overlapping range is one), a second
``others`` arm, and a list without ``others`` that does not cover every value
the selector can take are reported (IEEE 1076-2008 10.9, 11.5). Coverage is
computed from the lowered constants: an enumeration's literals, both values of a
scalar, or every combination of a vector up to 16 bits.

**Concurrent statements.** ``emitConcurrent`` lowers each statement into the
module. An instantiation either drives the entity's base module or, when it
binds generics or fixes unconstrained ports, a specialization cloned per
binding. A concurrent assignment is a continuous assignment, unless it carries
an ``after`` clause, in which case it is a timed always that waits and then
drives. A whole-port association compares the actual's type with the formal's
before connecting it, and an unassociated or ``open`` input takes the port's
declared default.

**Driver count.** Once every statement has been emitted, ``checkDrivers`` walks
the module and counts, per bit position, which statement drives each bit of a
signal whose type has no resolution function: two statements on the same bit are
reported (IEEE 1076-2008 12.6.4). Bit positions matter because a composite
signal's scalar subelements are separate signals, so element and record-member
writes do not collide; a static element index is folded for the comparison,
while a dynamic one conservatively covers the whole signal. `std_logic` and the
arrays built from it are resolved and exempt.

**Reports and termination.** ``report`` lowers to a display; ``std.env.finish``
lowers to ``$finish``. A report whose message is not a literal is dropped, since
only a literal is modelled, but a literal message is a string whatever its
characters are -- the parser keeps a message whole instead of retyping it as a
bit string. A report or assertion of ``severity failure`` ends the run at the
statement: the statements after it are not lowered, and after a failing
assertion they run only when it did not fail. A process whose only stopper is
such a failure runs once, at time zero.

_______________
What it may not do
_______________

The frontend may not add a V3 AST node, a V3 pass or a runtime primitive. Every
construct is expressed with nodes the middle end already has: ``AstAlways``,
``AstAssign``, ``AstAssignDly``, ``AstIf``, ``AstCase``, ``AstLoop`` with
``AstLoopTest``, ``AstDelay``, ``AstInitial``, ``AstInitialStatic``,
``AstDisplay`` and ``AstFinish``. Where a construct has no such expression, the
frontend refuses it with a diagnostic rather than extending the middle end, and
the finding is recorded instead.

Two consequences follow, and both have caused bugs worth knowing about:

- ``AstInitialStatic`` is the time-zero form and must not contain a delay; a
  timed body needs ``AstInitial``.
- ``AstNode::addNext`` requires its first node, so an empty statement list is a
  head, not a gap to be linked into.

_______________
Testing
_______________

Every construct the frontend supports is pinned by a test in ``test_regress/t``
that fails without it. The suite checks generated C++ values directly; no
external simulator is invoked. GHDL and nvc are used only for external corpus
sweeps that are not reproducible from this repository. The current coverage
and remaining correctness risks are assessed in :doc:`review`.