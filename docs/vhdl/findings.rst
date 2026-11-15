.. DESCRIPTION: Verilator: native VHDL frontend findings
   SPDX-FileCopyrightText: 2026-2026 Hannes Klas
   SPDX-License-Identifier: CC0-1.0

############################
Native VHDL frontend findings
############################

Status of the gaps found by running the native VHDL frontend
(``--vhdl-frontend native``) over the external corpus in
``vhdl_references/corpus`` and over ``third_party``. Every finding below was
closed by a frontend-only change unless it says otherwise: no new AST node, pass
or runtime primitive was added to the V3 middle end. :doc:`architecture`
describes the frontend these findings are about.

Each finding was closed the same way: a minimal reproducer, the value confirmed
with an independent simulator (GHDL, and nvc where GHDL and the frontend
disagreed), then a permanent regression test that fails without the fix.

______
Closed
______

.. list-table::
   :header-rows: 1
   :widths: 8 30 42 20

   * - id
     - symptom
     - resolution
     - test
   * - N13
     - a predefined unconstrained array as an array element type
     - ``integer_vector`` is modelled as an array of ``integer`` with the use's
       bounds
     - ``t_vhdl_elem_vector``
   * - N18
     - a package's ``real`` constant invisible outside the package
     - ``PackageInfo`` carries real constants; the body publishes and importers
       copy them
     - ``t_vhdl_pkg_real``
   * - N20
     - a slice whose discrete range opposes the array's direction accepted
     - a range designator that is a declared subtype runs with the array's
       direction and is refused otherwise; a literal range stays a documented
       extension
     - ``t_vhdl_slice_dir_bad``
   * - N22
     - an alias whose mark's range names an attribute of an object in the same
       declarative part
     - object aliases are registered after the part's declarations, subprogram
       aliases before
     - ``t_vhdl_alias_attr``
   * - N24
     - an aggregate as an operand of an operator has no target type
     - the type is derived from the choices (element from the first value, bounds
       from the choices)
     - ``t_vhdl_aggreg_operand``
   * - N29
     - a package specification constant initialized by a function whose body is in
       the package body
     - the specification's pass defers it; the body's pass completes it and
       publishes the value
     - ``t_vhdl_pkg_spec_fn``
   * - N30
     - ``real`` modelled as a ``double``
     - IEEE 1800 leaves ``real`` precision implementation-defined; both
       precisions conform, so no change is correct
     - ``t_vhdl_type_attr``
   * - N31
     - an attribute of a type mark with an argument refused in a value path
     - the attribute is folded before the refusal
     - ``t_vhdl_attr_arg``
   * - N32
     - a pure call whose actual is not static folds forever (87 GB)
     - measured unreachable: the statement evaluator refuses the form and both
       loop entry points cap the unroll; the refusal is pinned
     - ``t_vhdl_fold_nostatic_bad``
   * - N34
     - a ``std_ulogic_vector`` subtype whose constraint names a constant of the
       same block does not see it
     - deferred types wait for their part's constants
     - ``t_vhdl_defer_type``
   * - N35
     - a process ``for`` loop range must fold
     - a range that does not fold becomes a runtime loop over a fresh variable
     - ``t_vhdl_loop_range``
   * - N37
     - a VHDL testbench can neither be elaborated nor self-checked
     - ``report`` prints, ``std.env.finish`` ends the run, ``wait for`` lets time
       pass, a timed process repeats, and a delayed concurrent assignment drives a
       clock
     - ``t_vhdl_tb``, ``t_vhdl_tb_conc``
   * - N38
     - t80 faults the frontend with a null condition
     - the final ``else`` of a process ``if`` returns its branch body instead of an
       ``AstIf`` with no condition
     - ``t_vhdl_proc_else``
   * - N39
     - a ``file`` declaration refused wherever it appears
     - skipped in all six declarative parts
     - ``t_vhdl_file_decl``
   * - N40
     - a ``configuration`` design unit refused
     - parsed and checked against the architecture: a binding that matches what the
       architecture already instantiates changes nothing; a component, missing
       instance or different entity is refused with a diagnostic
     - ``t_vhdl_config``, ``t_vhdl_config_comp_bad``
  * - N41
    - an entity instantiated by an architecture written before it has no ports
    - entities are lowered children first, so a cell's pins exist when the cell is
      built
    - ``t_vhdl_inst_order``
  * - N42
    - a generate statement's declarative part refused
    - an attribute, default or disconnect spec is skipped and a component
      declaration is parsed and dropped
    - ``t_vhdl_gen_decl``
  * - N44
    - a bit-string literal that states its length, e.g. ``6x"3f"``, refused
    - the literal is padded to the length it states; a length that would drop a set
      bit is refused as the truncation the standard forbids
    - ``t_vhdl_bit_length``, ``t_vhdl_bit_len_bad``
  * - N45
    - ``min`` and ``hr`` refused after a numeric literal
    - the predefined units of ``time``, VHDL-2008 set, became one table of scales
    - ``t_vhdl_physical``
  * - N46
    - a ``while`` loop in a folded function, and a ``for`` range wider than the
      loop that runs
    - the fold re-tests the condition on every iteration as it does for a ``for``
      loop, and the bound counts the iterations folded rather than the range's size
    - ``t_vhdl_fn_loop``, ``t_vhdl_fn_loop_bad``
  * - N47
    - an exponent or a slice bound that is static only after lowering
    - the static evaluator lowers the operand and folds it, so a call folded to a
      constant answers the bound
    - ``t_vhdl_fn_loop``
  * - N48
    - an unconstrained multi-dimensional array type kept only its outer level, and
      a ``t(r1, r2)`` constraint refused
    - every index level survives so a use constrains each one, and the comma form
      feeds the nested constraints the chained ``t(r1)(r2)`` already used
    - ``t_vhdl_2d_array``
  * - N49
    - a comment pragma ``translate_off`` region elaborated as design code
    - the lexer drops a region's tokens; the pragmas nest, and any tool prefix
      counts as long as the pragma word ends the comment
    - ``t_vhdl_translate_off``
  * - N50
    - a user-defined physical type and its units: ``type FREQ is range ... units
      Hz; kHz = 1000 Hz; end units;``, read as ``1 MHz``
    - the parser keeps an abstract literal and the unit name beside it; the
      lowering resolves that name to a multiple of its type's step unit through a
      table built over the whole design, since a literal names its unit and not
      its type. Two types giving one unit name different multiples stay refused:
      only the expected type could tell them apart
    - ``t_vhdl_physical_units``, ``t_vhdl_physical_units_bad``,
      ``t_vhdl_physical_units_unsup``

____
Open
____

.. list-table::
   :header-rows: 1
   :widths: 8 30 62

   * - id
     - symptom
     - why it is open
   * - N15
     - ``%Warning-MULTITOP`` for an entity only ever instantiated with a generic
       map
     - the base module has no cell and looks like a second top. Deleting it is the
       only frontend lever, and two gates (frontend counters, then the module's own
       cell record) both leave 12 tests broken, so an uninstantiated module is not
       droppable here. Not making it a top needs ``AstModule::isTop()``
       (``level() == 1``, set by ``V3LinkCells``), which is the middle end.
       Reproducer: ``test_regress/t/t_vhdl_spec_top_repro.vhdl``.
   * - N43
     - an aggregate that does not cover every element of its target
     - the coverage rule (choices naming fewer elements than the target has, with
       no ``others``) is not modelled. This is poc-divider's last error.
  * - N51
    - ``shl`` and ``shr`` from ``IEEE.std_logic_misc``
    - no implementation of that vendor package is in the corpus and none is
      modelled. patmos' fpu is its only user (9 errors), so this is a library
      question rather than a frontend one.
  * - N52
    - a concurrent assignment to an ``inout`` port
    - modelling two drivers needs bus arbitration, which the frontend has no notion
      of. neo430's TWI, patmos' I2C and bit-serial each run into it (8 errors).

___________
Verification
___________

Each run was checked with the full native suite (``test_regress``,
``t_vhdl_*.py``) and against the corpus:

- lints: poc-divider, hdl-modules-fifo, neotrng, lfsr, ring-buffer,
  spi-single-cs, spi-fpga-slave, spi-fpga-master
- runs: fifo (153 checks), ring-buffer, neotrng, lfsr
- traces: neotrng and lfsr cycle for cycle against GHDL

Two simulators are used as ground truth: GHDL, and nvc (``nvc --std=2008``)
whenever GHDL and the native frontend disagree. nvc settled the value of a toggle
flip-flop sampled at 22 ns, which the frontend had initially gotten wrong by one
clock edge.

The corpus is also swept whole, not only through its ported testcases: every
non-testbench VHDL of the 20 cores is linted at once and the refusals are
histogrammed by message. Over this session the RTL refusals went from 251 to 182:
neo430 45 to 4, poc 51 to 20, open-logic 7 to 2, potato 17 to 13, microwatt 3 to
2. Physical literals in generic clauses were the last parse-level family in that
histogram: 15 poc refusals, all gone once a user-declared physical type is read.
What remains of patmos' 99 is its argo wrappers, whose ``work.ocp`` and
``work.argo_types`` packages are not in the repository at all, plus the
``std_logic_misc`` gap above (N51). The sweep also names the constructs still
refused in RTL that no core in the corpus happens to combine with a testbench,
which is where the next finding comes from.

The ported testcases were re-run after each change: 11 run scenarios and 4 cycle
traces (395, 612, 615 and 5621 cycles) pass against the native frontend.