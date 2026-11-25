..
   DESCRIPTION: Verilator: RFC for a native VHDL frontend
   SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
   SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#######################################
RFC: native VHDL frontend for Verilator
#######################################

Status: draft for maintainer discussion. This is a design proposal, not a
pull request. Please decide the scope questions in section 11 before review
effort is spent on the working branch of about 490 commits.

AI assistance: nearly all of the frontend, its tests and its documentation
were written with heavy assistance from an AI coding agent, under the
author's direction. As the project's AI policy requires, the author is
responsible for the contribution and is reviewing the branch before any
pull request. The regression suite, the open-source corpus and the
comparisons against GHDL described in section 7 are further checks, not a
substitute for that review. The provenance of generated code is also a
licensing question (see "Licensing of the corpus").

In short: the frontend parses the VHDL-2008 that lowers to Verilator's
existing two-state model and lowers it straight into the existing pre-width
V3 AST, adding no AST node, pass or runtime primitive. The core diff is
about 180 added lines in nine files; everything else is new files under
``src/V3Vhdl*`` plus tests and documentation. The decision most needed is
section 5's: whether a committed pre-width AST is acceptable.

1. Motivation
=============

VHDL IP commonly sits inside otherwise-SystemVerilog SoCs. Today the
workflow needs an external analyzer or a VHDL-to-Verilog translator, with a
second set of diagnostics and a translation boundary that hides the real
source location. ``--language vhdl`` should be a first-class Verilator
mode: one invocation, one AST, one set of diagnostics, no external process.

Alternatives considered:

- **External translator** (GHDL synthesis through Yosys to Verilog): the
  current workflow; see "Why not the GHDL/Yosys flow" below.
- **Out-of-process analyzer via GHDL's XML export:** this branch first took
  that route and abandoned it as too fragile; see "History of this branch"
  below.
- **Linking GHDL or nvc as a library:** brings a large dependency and, for
  GHDL, GPL licensing (GHDL and nvc are both GPL) into an LGPL/Artistic
  project.
- **Native frontend (this RFC):** costs maintainable code in the tree,
  which is why the scope is a bounded subset and the delivery is staged.

Why not the GHDL/Yosys flow
---------------------------

Today a VHDL design reaches Verilator by running ``ghdl --synth`` (usually
via ``ghdl-yosys-plugin``), writing Verilog with Yosys ``write_verilog``,
and verilating that. It works, and for full VHDL-2008 coverage it is the
stronger tool (see the last paragraph). The native frontend offers the
following that the translated flow does not:

- **One tool, no version matrix.** The flow needs GHDL (synthesis is on by
  default since 0.37, with a GNAT 8 or newer build toolchain), Yosys, and
  the ``ghdl.so`` plugin built against that GHDL. Its README calls the
  plugin "experimental and work in progress" and has no feature table of
  supported VHDL constructs. The native frontend needs only Verilator, and
  its behaviour is pinned by the tree and its tests rather than by
  whichever GHDL/Yosys the user installed.
- **Diagnostics point at the VHDL.** Verilator lint, width and
  ``MULTIDRIVEN`` style warnings on translated output name lines of
  generated Verilog, not the user's ``.vhd`` file. The native frontend
  reports at the VHDL source location, so ``-Wall`` and ``.vlt`` waivers
  apply to code the user actually wrote.
- **The model keeps the design's hierarchy and names.** A flow that runs
  Yosys ``synth`` flattens the design and maps it to cells with generated
  names. The native flow keeps one Verilator module per entity instance and
  the VHDL signal names, so waveforms, toggle coverage and
  ``--public-flat-rw`` access use names such as ``top.u_leaf.cnt`` (checked
  on a two-level design).
- **Testbench-style code in the model.** A Yosys netlist is a synthesized
  design, so constructs with no hardware meaning (``assert``/``report``
  with ``severity failure``, ``wait for``) are, to the best of my
  knowledge, not carried into the Verilog; I have not verified this against
  the plugin, whose README does not say. The native flow lowers them, so a
  self-checking VHDL bench can run on Verilator.
- **Simulation speed and compile time of the result are Verilator's
  concern.** A Yosys netlist is gate-level-like (many small cells and
  assigns) and gives the optimizer less to work with than word-level
  lowered RTL. This is a hypothesis to measure on the corpus, not a result
  yet; the RFC should not be accepted on it alone.
- **Mixed-language in one invocation.** An SV top instantiates a VHDL
  entity in the same run, with one set of options, and with
  ``--lib-create`` for a library flow, rather than a pre-step that produces
  a Verilog file per VHDL top.
- **No GPL tool in the loop and no process boundary.** Verilation does not
  shell out, which matters for hermetic build systems (Bazel, Nix) and CI.

Where the translated flow is better, and will remain so: GHDL implements
the full language, including 9-state ``std_logic`` resolution, user
resolution functions, and access, protected and file types, whereas this
frontend is a bounded two-state subset (sections 3, 4 and 8). A design
outside the subset should keep using the Yosys path. The case for the
native frontend is therefore diagnostics, structure, tooling integration
and dependency reduction for the VHDL that lowers to Verilator's model, not
wider language coverage.

History of this branch
----------------------

The first implementation ran GHDL out of process with ``--file-to-xml`` and
lowered the analyzed design units to the V3 AST. It worked for a small
subset, but the XML export is not a foundation for a compiler frontend.
GHDL documents ``--file-to-xml`` as a dump of the decorated syntax tree for
tooling such as style checkers and documentation extraction, warns that
"the AST slightly changes from time to time" (whenever nodes are added for
new language features) and that the XML "can be quite large so consider it
only during prototyping", and states that there is no XML dump of the
elaborated design. An importer built on it would track every GHDL release.
It was developed against a GHDL close to its git HEAD, while most users
have their distribution's GHDL, which is typically older, so the XML an
importer sees would differ from the one it was written for. No breakage was
observed; the risk is the documented instability combined with that version
spread. It also needs GHDL installed at verilation time, and diagnostics
depend on what GHDL emits.

The branch therefore moved to a hand-written in-tree lexer, parser and
lowering, and the GHDL-XML importer was removed entirely, so no GHDL code
or runtime dependency remains. The deciding argument is long-term
maintenance: a native frontend has its own bugs, but they are in-tree,
reproducible and fixable by Verilator maintainers, whereas breakage in an
external tool's internal dump format is not. GHDL is now used only as a
test oracle for the corpus trace comparisons in section 7.

2. Proposal
===========

Parse VHDL-2008 directly, elaborate it, and lower it into the existing V3
AST. There is **no new V3 AST node, no new pipeline pass, and no new
runtime primitive** that exists only for VHDL. There is no external
analyzer, no GHDL/nvc dependency, and no GPL-only component.

The scope rule follows from that: a construct is accepted when it lowers to
what the V3 AST and the runtime already express and gives the result GHDL
gives on the two-state subset. It is not limited to what a synthesis tool
accepts: ``assert``/``report``, ``wait for`` and external names are in,
because each lowers to an existing construct. A construct that needs a new
node, pass or runtime primitive only for VHDL is refused, however common it
is in VHDL. Files ending in ``.vhd`` or ``.vhdl`` select the VHDL frontend;
``--language vhdl`` selects it for other extensions.

3. Non-goals
============

- Full VHDL simulation semantics, including 9-state resolution and
  ``std_logic`` strength tracking.
- Access, protected, file and ``textio`` runtime types; user-defined
  resolution functions.
- VHDL-2019 as a baseline and floating-point packages.
- Any construct that needs a new V3 AST node, pass or runtime primitive
  that only VHDL would use.
- VHDL-to-SystemVerilog binding inside one elaborated netlist beyond the
  boundary described in section 6.

Constructs outside the subset are refused with a located diagnostic, not
silently ignored.

4. Semantics
============

.. list-table::
   :header-rows: 1
   :widths: 18 82

   - - Area
     - Contract
   - - Values
     - Runtime model is two-state. ``std_logic`` collapses to 0/1; ``'U'``,
       ``'W'``, ``'X'``, ``'-'`` are X in constants and 0/1 at run time.
       ``'Z'`` is kept for a tristate driver and resolves to 0 elsewhere.
   - - Drivers
     - One driver per non-tristate signal. Conflicting enabled tristate
       drivers resolve to 1 where VHDL gives ``'X'``. No user resolution
       functions.
   - - Inout
     - With ``--pins-inout-enables``, a top-level ``inout`` is an input
       plus ``name__en``/``name__out``; use this when an external driver
       exists. Without it the C++ interface exposes one two-state field
       holding the externally supplied value. There is **no guaranteed OR**
       of external and internal drivers and no IEEE 1164 resolution.
   - - Integers/ranges
     - ``integer`` is 32-bit signed; ``natural``/``positive`` are subtypes.
       Bounds are checked statically, not at run time. Enumeration encoding
       follows declaration order.
   - - Arrays/records
     - Packed into one wide vector; indexing becomes a bit select and a
       dynamic index a variable part-select. There is no per-element V3
       variable or type, so cost grows with packed width.
   - - Assert/report
     - Accepted. Static assertions may fold at elaboration; a literal
       report message prints, ``severity failure`` stops the run with a
       non-zero exit status like ``$fatal``, and a non-literal message
       prints the fixed text ``report`` (``Assertion failed`` for an
       assertion).
   - - Timing
     - A process with waits (several, or inside an ``if``, ``case`` or
       ``for``) and a nonzero ``after`` on a concurrent assignment lower to
       timed blocks under ``--binary``, at the absolute VHDL time whatever
       ``--timescale`` says. ``wait for`` is a delay, ``wait on`` and
       ``wait until`` an event control. A zero ``after`` is a delta. A
       nonzero ``after`` inside a process, ``transport``, ``inertial``,
       ``reject`` and a ``wait`` inside a ``while`` or bare ``loop`` are
       refused.

An object with no initializer starts at 0, where VHDL would give ``'U'`` or
``T'left``. This is a deliberate divergence for C++/SV users. Reset
randomization (``+verilator+rand+reset+N``) works as for SystemVerilog on
vectors and ``std_logic``; ``integer`` objects always start at 0, as an SV
``int`` does. The full divergence table is in
``docs/vhdl/limitations.rst``.

5. Architecture
===============

.. code-block:: text

   VHDL / SV source files
             |
             v
   readFiles: extension dispatch
             |
             v
   V3VhdlParse -> lexer -> parser / frontend AST
             |
             v
   Elaboration: packages, generics, instances
             |
             v
   Lowering (7 domain translation units)
             |
             v
   Committed pre-width V3 AST modules
             |
             v
   V3LinkCells -> V3VhdlLink (debug builds)
             |
             v
   V3Width -> existing middle end -> V3EmitC

Pass order in ``Verilator.cpp`` is unchanged. The frontend emits
**committed pre-width AST**: dtypes are already canonical, ``AstParseRef``
and ``childDTypep`` are gone, and every expression and variable has a
dtype. ``V3Width`` therefore leaves those nodes alone. This is the single
architectural decision that needs acceptance or rejection: an alternative
would be to emit parse-shape AST and add VHDL-aware width/link passes, at
the cost of new V3 semantics and a larger core diff.

``V3VhdlLink`` asserts the committed shape inside ``readFiles``, after
``V3LinkCells`` and before ``V3Width``. It runs in every debug build
without a flag and compiles out in release. ``t_vhdl_tree`` pins one
entity's emitted tree.

6. Integration
==============

- Core surface (about 180 lines added and 46 removed or rewritten in nine
  files, three of them build lists): one language enum and option plumbing
  in ``V3Options``, one ``V3Global`` bool and per-file dispatch in
  ``readFiles``, a 50-line change in ``V3LinkCells``, a two-line guard in
  ``V3Coalesce`` so an ``inout`` is not coalesced away, build-list entries,
  and the contract-check unit. No changes to pass order.
- Mixed language: an SV top may instantiate a VHDL entity in one
  invocation; a VHDL hierarchy can be built as a ``--lib-create`` library
  and instantiated from SV. A VHDL design cannot instantiate an SV module
  directly. Only design-unit instantiation crosses the boundary: no shared
  types, package imports or cross-language hierarchical references.
- Source layout: lexer, parser/AST, package elaboration, and seven lowering
  units (``types``, ``expr``, ``stmt``, ``subprog``, ``process``,
  ``package``, ``elab``) with two private headers. The largest unit is
  ~4.2k lines; the ``Lowering`` class declaration is ~1.5k lines.
- Frontend total is ~23.2k lines in 21 files. Against base ``53a72711`` the
  branch is about 490 commits, 1332 files, ~71k insertions (including
  tests).

7. Evidence
===========

- 414 regression drivers, 104 diagnostic/tree goldens, 414 ``.vhdl``
  fixtures, 280 value-checked C++ harnesses.
- Full VHDL suite (``t_vhdl*``, ``--vlt``): 414 passed / 0 failed in ~20 s
  on 16 jobs locally. Both distribution tests (``t_dist_test_style``,
  ``t_dist_copyright``) pass. The suite needs only the built Verilator and
  a C++ compiler; no test invokes GHDL or nvc.

Open-source corpus
------------------

Beyond the regression tests, the frontend runs against a corpus of 18
pinned open-source VHDL projects: PoC, open-logic, hdl-modules, microwatt,
neorv32, neo430, neoTRNG, potato, RPU, T65, T80, nandland spi-master,
spi-fpga and others. The corpus lives in a separate repository: a manifest
of pinned upstream revisions, a fetch script, and 88 ported testcases (the
table below covers the 86 run for this summary). Each testcase keeps the
upstream design unmodified, drives it from a self-checking C++ testbench,
and many also compare a cycle-by-cycle trace against GHDL. Upstream sources
are fetched, not copied, so each keeps its own license.

Licensing of the corpus
~~~~~~~~~~~~~~~~~~~~~~~

Nothing from these projects is part of Verilator, and Verilator links none
of them. The licenses differ and have not had a legal review:

- Permissive: PoC and RPU (Apache-2.0); neorv32, neo430, neoTRNG, fpga_puf,
  hdl-modules, potato, f32c and patmos (BSD-2-Clause or BSD-3-Clause);
  spi-master, spi-fpga, VHDL_Lib and bit-serial (MIT). T65 and T80 carry a
  BSD-style notice in their file headers and have no license file.
- open-logic uses the PSI HDL Library License 1.0: the GNU Library General
  Public License version 2 or later, with an exception that lets works in
  binary form or hardware containing binaries be distributed on the user's
  own terms. A Verilator model generated from its sources is a work based
  on the library, so distributing it, or a simulation binary built from it,
  needs the source offer and notices that license asks for.
- microwatt uses CC-BY-4.0, which requires attribution. Its notice also
  says the rights to implement the Power ISA are not granted by that
  license, so the same caution applies to a generated model.

The corpus repository commits only its own wrappers, shims, C++ testbenches
and generated program images, and fetches the upstreams at pinned
revisions. Some wrappers repeat a few lines of an upstream configuration
file; those lines stay under the upstream license and keep its notice. The
corpus will be published under the MIT license, which does not relicense
the fetched upstream sources. The regression fixtures in ``test_regress/t``
are independent reductions: a comment names the upstream construct that
motivated one, and no fixture shares a line of 35 or more characters with
the corpus sources. The legal review of the points above is still open.

Results on this branch (lint and simulation with Verilator, trace
comparison against GHDL 7.0.0-dev):

.. list-table::
   :header-rows: 1
   :widths: 22 14 64

   - - Project
     - Simulates
     - Notes
   - - open-logic
     - 32 / 35
     - All 32 match GHDL cycle by cycle: arbiters, clock crossings, FIFOs
       (sync, async, packet), RAMs, CRC, width converters, TDM, CAM
   - - hdl-modules
     - 13 / 13
     - FIFO, ring buffer, handshake pipeline/mux/merger/splitter, LFSR,
       debounce, resync; 7 also traced against GHDL
   - - microwatt
     - 5 / 7
     - Multiplier, PLRU, bitsort, SoC reset, wishbone BRAM; the caches do
       not yet elaborate
   - - neorv32
     - 1 / 2
     - The CPU runs a freestanding C program (12.2M cycles) to the expected
       results; the SoC top does not yet elaborate
   - - neo430, neoTRNG
     - 3 / 3
     - Multiplier/divider and CRC units; the TRNG matches a 5,621-cycle
       GHDL trace
   - - T80, RPU, spi-master, spi-fpga
     - 6 / 6
     - The T80 runs a Z80 program; RV32 ALU and register file; SPI masters
       and slave
   - - PoC
     - 4 / 17
     - Divider, square root, Gray counter, strobe generator; 13 others
       share one blocker in PoC's configuration package
   - - potato, T65
     - 0 / 3
     - An enumeration actual on a port of another type is not accepted

In total 64 of 86 testcases simulate with every check passing. All 45 GHDL
trace comparisons that build match cycle for cycle; none mismatches. Two of
the failures, open-logic's CAM and one PoC harness, fail only because
``--enable-ccwarn`` turns a C++ warning into an error. The rest fall into
three groups: a configuration-package blocker in PoC, enumeration actuals
on ports of another type (potato, T65), and elaboration gaps in microwatt's
caches and the neorv32 SoC top.

The corpus repository is not yet public, so these results cannot be
reproduced from a Verilator checkout. Publishing it is part of the
maintenance plan; until then the numbers are reported for information.

8. Known limitations
====================

- Dynamic-index driver counts and ``std_logic_vector`` ``MULTIDRIVEN``
  diagnostics are imprecise.
- ``std_logic_signed`` arithmetic and comparison, ``case generate``,
  enumeration range subtypes, resolution indications and some type
  attributes are not implemented.
- PSL directives are skipped. Library namespace handling is simplified.
- Waveforms keep the hierarchy and signal names, but an enumeration shows
  as its integer encoding and a record or array as one vector, in VCD and
  FST alike, because they are lowered to packed vectors.
- Use clauses are scoped per design unit, and the types of the IEEE
  packages must be made visible by one. The subprograms of those packages
  and the ``library`` clause itself are not checked: a unit that calls them
  nearly always has a signal or port of the matching type, which is
  checked, and the frontend resolves the subprograms by name, so a precise
  check is hard.
- Zero-width objects outside a whole-array assignment are not modelled;
  some legal constructs are falsely rejected as ``BLKANDNBLK`` or
  ``ASSIGNIN``.
- Large memories and some whole-core elaborations can exhaust memory.

9. Maintenance plan (proposal)
==============================

- Owner: Hannes Klas, who wrote the frontend, triages new unsupported
  constructs and keeps ``docs/vhdl/limitations.rst`` current.
- Every deliberate refusal goes through one helper with a source location
  and gets a diagnostic golden; a new accepted construct gets a
  value-checked C++ harness. Regressions are caught by the VHDL suite plus
  the two distribution tests.
- The frontend is always built; no optional CMake/Makefile configuration.
- Reviewers needed: one for the parser/elaboration boundary, one for the
  lowering units and the pre-width AST contract.

10. Staged delivery
===================

Each stage lands with its own tests and passes the full regression suite,
so the tree never holds a half-working ``--language vhdl``.

.. list-table::
   :header-rows: 1
   :widths: 8 52 40

   - - PR
     - Scope
     - Why it is reviewable
   - - 0
     - Language plumbing, extension dispatch, build lists, option tests
     - Small core change, no parser
   - - 1
     - Lexer, parser, frontend AST, first diagnostic goldens
     - Syntax and errors in isolation
   - - 2
     - Flat RTL lowering: entities, ports, signals, operators,
       ``std_logic``
     - First useful C++ model
   - - 3
     - Processes, sequences, variables, clocks and resets
     - Value-checked harnesses
   - - 4
     - Hierarchy, generics, packages, records, unconstrained ports
     - Hardest semantics, isolated
   - - 5
     - Subprograms, ``numeric_std``, attributes, reports
     - Library semantics last
   - - 6
     - Tristate/inout, ``--pins-inout-enables``, limited timing
     - Optional for a first merge
   - - 7
     - Mixed-language and ``--lib-create``
     - Defer unless required for merge

11. Questions for maintainers
=============================

#. Is VHDL in scope for Verilator at all?
#. Is emitting a committed pre-width AST acceptable, or must width/link
   gain VHDL-aware passes?
#. Which mixed-language boundary is required for a first merge?
#. Is a staged stack acceptable, or do maintainers prefer one feature PR
   with a reviewer guide?
#. What CI and performance bar must the first merge clear?
#. Who will review the lowering and who maintains it after merge?
