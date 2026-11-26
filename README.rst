..
   SPDX-FileCopyrightText: 2003-2026 Wilson Snyder
   SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

========================================================
Verilator, experimental fork with a native VHDL frontend
========================================================

**This is an experimental fork. For anything that matters, use upstream
Verilator:** https://github.com/verilator/verilator (releases, packages and
documentation at https://verilator.org).

Verilator compiles synthesizable Verilog and SystemVerilog into a C++ model
that is then built and run as a simulator. This fork adds a frontend that
reads VHDL-2008 directly, so that

.. code-block:: bash

   verilator --cc --exe --build -j 0 counter.vhdl sim_main.cpp
   ./obj_dir/Vcounter

verilates a ``.vhd`` or ``.vhdl`` design with one command, one AST and one
set of diagnostics, without GHDL or a VHDL-to-Verilog translation step.
Everything after analysis is unchanged upstream Verilator: the same
generated C++, the same runtime library and the same testbench idiom.

Status
======

The fork is a working proposal, not a supported product. Treat it as
follows:

- It is not released, versioned or packaged, and it is not endorsed by the
  Verilator maintainers. Behavior, option names and the supported VHDL
  subset can change or disappear.
- Nearly all of the frontend, its tests and its documentation were written
  with heavy assistance from an AI coding agent, under the direction of the
  fork's author, who is responsible for the contribution and is reviewing
  it. The provenance of generated code is also a licensing question that
  has not had a legal review.
- The runtime model is two-state. A VHDL design that depends on
  ``'U'``/``'X'`` propagation or nine-state resolution simulates
  differently here than in GHDL or a commercial simulator.
- Constructs outside the supported subset are refused with a diagnostic,
  but the refusal list is not complete, and a few legal constructs are
  falsely rejected. Large memories and whole-core elaborations can exhaust
  memory.
- Mixed-language support is one-way: a SystemVerilog top can instantiate a
  VHDL entity, not the reverse.

A design that must be correct should be checked against a reference VHDL
simulator, or run through the ``ghdl --synth`` and Yosys flow, which covers
more of VHDL-2008 than this frontend does.

Try it
======

Build the fork like upstream Verilator, in the source tree. Configure with
``--enable-ccwarn`` so a new compiler warning stops the build:

.. code-block:: bash

   git clone https://github.com/hun/verilator.git
   cd verilator
   git checkout vhdl-native-frontend
   autoconf && ./configure --enable-ccwarn && make -j8

Then follow the worked counter and hierarchical-accumulator examples in
``docs/vhdl/usage.rst``. The file extension selects the language, so no
VHDL-specific option is needed; ``--language vhdl`` covers VHDL kept in
files with other extensions.

Run the VHDL regression tests from the repository root. The suite needs
only the built Verilator and a C++ compiler; it does not call GHDL:

.. code-block:: bash

   test_regress/t/t_vhdl_*.py

Check what the frontend accepts
===============================

``docs/vhdl/limitations.rst`` lists the supported subset and each place
where the model diverges from VHDL: the two-state value model, delta-cycle
approximation, packed records and arrays, and how enumerations and records
appear in waveforms (as plain vectors). Read it before porting a design.
``docs/vhdl/architecture.rst`` describes how the frontend lowers VHDL into
Verilator's AST, and the design adds no AST node, pass or runtime
primitive.

The branch has been run against a corpus of open-source VHDL projects (PoC,
open-logic, hdl-modules, microwatt, neorv32, T80 and others). At the last
count 87 of 88 ported testcases simulate with every check passing; the other
one builds a model that its C++ compiler rejects under ``--enable-ccwarn``. The
corpus repository is not public yet, so these numbers cannot be reproduced from
this checkout.

Read the proposal
=================

``rfc_native_vhdl_frontend.rst`` is the request for comments written for
the upstream maintainers: motivation, design, evidence, known limitations,
a maintenance plan and a staged way to merge the work. Whether any of it
goes upstream is the maintainers' decision.

Relation to upstream
====================

The branch is based on upstream ``master`` and adds ``src/V3Vhdl*``, a
small change in nine existing files, tests under ``test_regress/t/t_vhdl*``
and the documentation named above. The original upstream README is kept as
``README.upstream.rst``. Bugs that also occur with SystemVerilog input
belong in the `upstream issue tracker
<https://github.com/verilator/verilator/issues>`_; report VHDL frontend
problems on this fork instead, and do not ask the upstream project to
support it.

Verilator is copyright Wilson Snyder and contributors, and is licensed
under LGPL-3.0-only OR Artistic-2.0. The additions in this fork carry the
same license.
