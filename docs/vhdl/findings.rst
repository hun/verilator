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
  * - N53
    - one index of a multi-dimensional object: ``T'LEFT(2)``, ``T'RANGE(1)``, a
      constraint list ``t(v'range, n - 1 downto 0)`` and ``t(open)(1 downto 0)``
    - the level index is read once, in a helper every consumer shares, so a
      constraint, a slice, a subtype and a loop range all name the same range.
      ``open`` leaves an index to the object that names the subtype, so only the
      element level has to be concrete. A designator may prefix a range
      attribute, as in ``v(v'low)'range``
    - ``t_vhdl_array_attr_index``, ``t_vhdl_index_constraints``
  * - N54
    - one element of an array a function returns: ``f(x)(0)``
    - a call's declared return type now types the designator, so the element comes
      from the array and not from the returned value's flat data type. The
      resolution is only made for a type mark that names a declared array type,
      since an unconstrained ``std_logic_vector`` return has no element width to
      give; a conversion keeps selecting a bit
    - ``t_vhdl_func_result_index``
  * - N55
    - a record constraint whose member takes its range from an attribute:
      ``rec_t(f(3 downto 0), g(x'range))``, and the same list chained after
      ``open``: ``t_arr(open)(f(r))``
    - a member's range is either written out or named by an attribute, which
      the lowering resolves like any other range attribute. A chained list is
      told apart from an index range by the range inside the member's own
      parentheses, so ``t(v'range)(v(v'low)'range)`` still parses as two index
      ranges. A record with unconstrained elements is what makes the constraint
      legal, so the ``open`` and record forms are what poc's AXI4 packages use
    - ``t_vhdl_record_constraint``
  * - N56
    - ``T'element`` as a type mark: a register declared ``BCDDigits'element``,
      naming a port
    - the attribute names the element type of an array, and the name before it
      may be a type or an object; the mark is resolved either way, so poc's
      generate block can give a pipeline register the element type of the bus
      it drives
    - ``t_vhdl_element_type``
  * - N57
    - a context declaration: ``context common is ... end context;``, a bundle of
      library and use clauses under a name
    - the declaration has no design unit of its own, so it is skipped whole, which
      is what poc's common.vhdl needs to parse. A context *clause* elsewhere still
      imports nothing, so a reference is only as good as the use clauses next to it
    - ``t_vhdl_context_decl``
  * - N58
    - an if-generate condition with a label: ``gen: if gInfer: (VENDOR = ...) or
      ... generate``
    - the label is a name and nothing else; GHDL and nvc both take the expression
      after it as the condition, which is what poc's ocrom ROMs rely on, so the
      label is dropped and the rest parsed as the condition
    - ``t_vhdl_labelled_cond``
  * - N59
    - a signal assignment with a delay: ``q <= d after delay;``, as a gate-level netlist
      writes it
    - a delay of zero is a delta, which a cycle model already has, so it is parsed and
      dropped; a longer delay inside a process needs a scheduled update and is
      reported. A concurrent assignment keeps its delay, which lowers to a wait
    - ``t_vhdl_after``, ``t_vhdl_delay_unsup``
  * - N60
    - one element of a function result whose element type is an unconstrained vector
    - the element has no width to select, so the select reached the emitter with
      zero width and faulted; the index is reported instead. A function returning a
      plain ``std_logic_vector`` still selects one bit, which is what its type says
    - ``t_vhdl_elem_unsup``
  * - N61
    - a sequential ``assert`` never ended the run: the parser dropped both the
      message and the severity, and the statement reached the model as a report
    - the statement keeps its condition, message and severity. Severity note,
      warning and error report and carry on; ``severity failure`` reports and
      ends the run (IEEE 1076-2008 8.3), which is what a testbench needs to
      stop on a failed check. A concurrent assertion is still ignored, as before
    - ``t_vhdl_assert_severity``, ``t_vhdl_assert_finish``
  * - N62
    - an entity with a statement part: ``entity e is ... begin assert ...; end``,
      as f32c's DDR2 input writes its generic checks
    - the statement part is parsed into the entity and emitted into the module, and
      a concurrent assertion reports on every evaluation and ends the run on
      ``severity failure``, sharing the body the sequential form uses
    - ``t_vhdl_entity_assert``
  * - N63
    - a case in a folded function body that covers every value of a selector with
      no ``others`` arm, as potato's context-to-vector conversion does
    - the fold evaluated each arm and then refused, because the chain had no default.
      A selector that matches nothing now leaves the values the body had before the
      case (IEEE 1076-2008 10.9); a case that cannot return on that path reports
      that
    - ``t_vhdl_case_cover``, ``t_vhdl_fn_case_bad``
  * - N64
    - a selected assignment whose choice is an expression of constants:
      ``when F7_OP_ADD & F3_OP_ADD =>``, as rpu's ALU writes it
    - the choice is folded to a value before it is compared, instead of having to be a
      literal
    - ``t_vhdl_choice_expr``
  * - N65
    - an alias in a process declarative part: ``alias j : glob_jtag_t is glob_jtag;``,
      as microwatt's sim_jtag writes it
    - the alias is parsed and registered with the process's other declarative items, so
      it denotes the same designator as an architecture-level alias
    - ``t_vhdl_proc_alias``
  * - N66
    - an instantiation whose entity is a selected name: ``u : work.pkg.child generic
      map (...) port map (...)``, as bit-serial's UART instantiates its FIFO
    - the instantiation lookahead only looked one token ahead, so a qualified entity
      name reached the parser as a procedure call; the lookahead now walks the
      selected name
    - ``t_vhdl_selected_inst``
  * - N68
    - a conversion to a predefined integer subtype or to numeric_std's
      unresolved vector: ``natural(x)``, ``positive(x)``, ``u_unsigned(x)``,
      ``u_signed(x)``, as hdl-modules' ``math_pkg`` and
      ``lfsr_fibonacci_multi`` write them
    - the conversion list only named ``integer``, ``unsigned`` and ``signed``,
      so the other type marks reached the parser as calls and were reported as
      unknown identifiers
    - ``t_vhdl_conv_predefined``
  * - N70
    - an inout port: a cycle model has no tristate, so nothing resolves its
      driver
    - the port reached the DFG undriven and failed the read-only assertion
      there; it is now refused where it is declared, and declared as an input so
      the rest of the design still analyzes
    - ``t_vhdl_inout_unsup``
  * - N71
    - the std_logic_1164 X01 conversions ``to_x01``, ``to_01``, ``to_x01z`` and
      ``to_ux01``, as t80's ALU writes them
    - the intrinsic list had the ``is_*`` predicates but not the ``to_*`` forms,
      so the calls were reported as unknown identifiers
    - ``t_vhdl_std_logic_fn``
  * - N72
    - an alias of a signal in a process sensitivity list and in an edge call:
      ``alias clk is wb_clk_i; process (clk)``, as microwatt's 16550 writes it
    - the name lookup searched the scope only, and an alias is registered beside
      it, so the process reported an unknown signal
    - ``t_vhdl_sens_alias``
  * - N73
    - an unconstrained vector constant or generic with a literal default:
      ``constant c : std_logic_vector := "0011"``, as f32c's clock generics are
      written
    - the declared type leaves the width open, so resolving it reported an
      unconstrained type; the literal's width is all that is missing, and the
      constant now takes it
    - ``t_vhdl_unconstrained_gen``
  * - N74
    - a constant expression with real operands: ``integer(1000000000.0 / real(c_ns)
      / real(c_freq) * 268435456.0)``, as f32c's prescaler is written
    - the real value reached the model, which has no real type; a constant operand
      now folds first, and a real expression is evaluated as a real so that each
      step does not truncate
    - ``t_vhdl_real_const``
  * - N75
    - a string generic passed on to a sub-instance:
      ``generic map (RAM_INIT_FILE => RAM_INIT_FILE)``, as microwatt's
      ``wishbone_bram_wrapper`` binds its RAM
    - the actual had to be a literal, in the parser and in the lowering; a
      simple name now resolves from the enclosing unit's string generics, the
      same way a generic type that is passed on does
    - ``t_vhdl_str_pass``
  * - N76
    - ``std.textio`` used to build a memory image at elaboration: microwatt's
      ``fpga/main_bram.vhdl`` reads ``RAM_INIT_FILE`` with ``readline``,
      ``hread`` and ``endfile`` into an ``impure function ... return ram_t``
    - a file has no runtime meaning, so the file declaration is parsed rather than
      skipped and the body is evaluated while the design is elaborated: an
      interpreter in the lowering runs its ``for``/``exit when``/element
      assignment/``return`` against the file and the result becomes a constant.
      ``hread`` fills as many hex digits as the target is wide and leaves the rest
      of the line unread, which is what GHDL and nvc both do. Nothing reaches the
      middle end or the runtime
    - ``t_vhdl_file_init``
  * - N77
    - a record aggregate that leaves the rest of the fields to ``others``:
      ``constant wishbone_slave_out_init : wishbone_slave_out := (ack => '0',
      stall => '0', others => (others => '0'));``, as microwatt's
      ``wishbone_types`` writes it
    - ``others`` was only handled for arrays, so a record aggregate reported
      ``record has no element 'others'``. The remaining fields are now filled whole,
      one field at a time, with the last association's value
    - ``t_vhdl_record_agg``
  * - N78
    - a package constant whose value is a call into another package:
      ``constant TAG_NUMBER_BITS : natural := log2(TAG_COUNT);`` and a subtype
      sized by it, as microwatt's ``common`` writes them
    - the call cannot be folded while the specifications are read, because the
      called body comes later, so the constant was deferred. The deferred value was
      then completed during the *next* unit's emission, whose scope is not the one
      it was declared in, so the name it calls was reported unknown. A deferred
      constant is now completed by a further pass over its own unit, after that
      unit's other constants, and a type naming it waits for that pass
    - ``t_vhdl_pkg_fn_const``
  * - N79
    - a discrete subtype where a range is expected:
      ``type predec_array is array(index_t) of predec_t;`` and
      ``for i in index_t loop``, as microwatt's ``predecode`` writes them
    - an index constraint and a loop range accepted only a literal range, an
      attribute or (for an array index) an enumeration mark. A subtype whose
      kind is integer now supplies its own bounds, ascending when low <= high
    - ``t_vhdl_subtype_range``
  * - N80
    - ``std_match(iword, "01100000000000000000000000000000")`` from
      ``ieee.std_logic_1164``, as microwatt's ``predecode`` compares opcodes
    - not modelled at all, so the call was an unknown identifier. It lowers to
      an equality under a mask of the bits that are not ``-`` wildcards, which
      the lexer already keeps in a bit-string literal for exactly this
    - ``t_vhdl_subtype_range``
  * - N81
    - a slice whose bounds are variables holding static values:
      ``lbit := i * BYTEWID; ... ram(widx)(mbit downto lbit) <= ...``, as
      microwatt's ``cache_ram`` writes the byte lanes
    - the bounds were folded without the tracked variable values, and entering
      an ``if`` or ``case`` discarded them outright, so a bound assigned before
      the statement was unknown inside its branches. A branch now starts from the
      values that held before the statement, and a slice bound substitutes them
      before it folds
    - ``t_vhdl_subtype_range``
  * - N82
    - a process variable whose name is also an enumeration literal of a used
      package: ``variable ra : real_addr_t;`` in microwatt's ``icache``, where
      ``ra`` is also a literal somewhere in the packages it uses
    - a local variable was registered without reserving its name, so the lookup
      found the imported enumeration literal first and reported ``cannot assign
      to constant 'ra'``. A variable now hides a package member as a signal does
    - ``t_vhdl_var_shadow``
  * - N83
    - a function that writes a slice of one of its own variables, called with a
      record element as the actual: ``r.row <= next_row(r.row)`` with
      ``v(2 downto 0) := std_ulogic_vector(idx + 1)`` in the body, as microwatt's
      ``icache`` advances its row counter
    - the body is folded by rebuilding the local's value around each element
      write, and the offset summed the selects that binding the actual had
      brought along (the record element sits one bit up) on top of the target's
      own. The sum now stops at the level whose width is the local's, so only the
      target's selects count. In ``icache`` this left the row counter stuck, the
      line never completed, and every fetch after the first was never answered
    - ``t_vhdl_fn_slice_formal``
  * - N85
    - a call where a *width* is expected: potato's icache names an aggregate with
      ``(log2(LINE_SIZE * 4) - 1 downto 0 => '0')`` and a conversion with
      ``to_unsigned(cl_current_word, log2(LINE_SIZE))``
    - both bounds and the conversion width were folded with the plain constant
      folder, which cannot read a call, so an aggregate in a concatenation had no
      type and the conversion had no width. They now use the folder that inlines a
      pure function, the same one a type range uses (N78)
    - ``t_vhdl_folded_width``
  * - N84
    - a trace of a VHDL design: ``--trace`` with ``--vcd`` or ``--fst``
    - the model registers no signals at all, so the dump has an empty definition
      section. Reviewing a VHDL design's internals needs the dump a simulator
      that has the feature produces (``ghdl --vcd``), or the frontend's own
      ``--public-flat-rw`` accessors
  * - N87
    - an unconstrained ``string`` constant: neorv32's CPU names its trace file
      ``"neorv32_trace" & natural'image(HART_ID) & ".log"``
    - the ``string`` mark resolved to a one-element array, so the initialiser was
      truncated to one character, and ``'image`` with an argument was refused by
      name. A string constant now takes its bounds from its value, a literal or a
      static expression, and ``'image`` folds to the decimal of an integer or the
      literal name of a Boolean value (IEEE 1076-2008 16.2.2)
    - ``t_vhdl_image_attr``
  * - N88
    - an entity no instance names, whose generics have no defaults: neorv32's
      ``neorv32_prim_*`` entities and hdl-modules' modules
    - the entity was reported as having an unbound generic, and its empty module
      was left behind, which then looked like a second top level. An entity that no
      instance names is not part of the design, so it is dropped; a referenced one
      still reports the unbound generic at its instance
    - ``t_vhdl_unused_entity``
  * - N89
    - a record aggregate as a whole-port actual:
      ``ibus_rsp_i => (ack => i_ack, err => i_err, data => i_rdata)``, as the
      neorv32 port shim connects the CPU's buses
    - the aggregate was lowered against the port's packed vector, whose element
      names are not those of the record, so its field choices read as array
      choices. A whole-port actual or default now takes the record type the port is
      declared with
    - ``t_vhdl_record_port_agg``
  * - N86
    - a concurrent ``assert`` whose condition is a pure function of generics:
      ``assert is_power_of_two(segment_length_bytes)``, as hdl-modules'
      ``ring_buffer_write_simple`` writes its width checks
    - the condition was lowered, and lowering a call inlines the body, so the
      math helper reached ``real(value)``, which has no synthesizable form, and
      the design was refused. A static condition now folds to a constant before
      any body is lowered, and a pure function whose body declares constants
      folds too, as ``constant log2_value : natural := floor_log2(value);``
      does
    - ``t_vhdl_static_cond``

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
  * - N67
    - the simulation-only subset: a ``line`` variable *written* by a debug
      procedure through ``ieee.std_logic_textio`` (bit-serial; reading a file into
      an image is N76), poc's ``protected`` type definition, and
      neorv32's trace file name ``"neorv32_trace" & natural'image(HART_ID) & ".log"``
    - none of them has a synthesizable meaning: an access type with file operations,
      a semaphore built for shared variables, and a string constant feeding a trace
      logger. A cycle model cannot represent them, so the refusal is deliberate; a
      design that only uses them in a simulation branch keeps everything else
  * - N90
    - a model of neorv32's CPU core (``neorv32_cpu``, RV32I) never updates any
      state: every register, including the fetch state, keeps its initial value
    - the frontend lowers the design (the reset branch of the fetch process is
      present after linking), but the generated model's sequential regions never
      run, so no instruction is fetched. GHDL simulates the same files with the
      same testbench and fetches correctly (19 fetches in 40 cycles), so the
      refusal is in the model rather than in the testbench. The corpus keeps the
      port in its ``blocked`` target until this is understood
  * - N51
    - ``shl`` and ``shr`` from ``IEEE.std_logic_misc``
    - no implementation of that vendor package is in the corpus and none is
      modelled. patmos' fpu is its only user (9 errors), so this is a library
      question rather than a frontend one.

___________
Verification
___________

Each run was checked with the full native suite (``test_regress``,
``t_vhdl_*.py``): 186 passed, 2 skipped (the two GHDL-only diagnostics). The
GHDL frontend passes 114 with 3 pre-existing failures (``t_vhdl_genfunc``,
``t_vhdl_genpkg``, ``t_vhdl_range_attr``).

The corpus lints and runs clean: 19 designs, no lint errors, and every run
passes, including the harnesses added for the rpu ALU, the rpu register set, the
t65 CPU and microwatt's ``soc_reset`` and ``wishbone_bram_wrapper``. The last of
those runs ``fpga/main_bram.vhdl`` unmodified, its ``RAM_INIT_FILE`` read at
elaboration from a checked-in image. hdl-modules' ``ring_buffer_write_simple``
runs too, its width checks folded at elaboration (N86).

Two simulators are used as ground truth: GHDL, and nvc (``nvc --std=2008``)
whenever GHDL and the native frontend disagree. nvc settled the value of a toggle
flip-flop sampled at 22 ns, which the frontend had initially gotten wrong by one
clock edge.

The corpus is also swept whole, not only through its ported testcases: every
non-testbench VHDL of the 20 cores is linted at once and the refusals are
histogrammed by message. Over these sessions the RTL refusals went from 251 to
175, measured after the index-range attributes landed: neo430 45 to 4, poc 51 to
19, open-logic 7 to 2, potato 17 to 7, microwatt 3 to 2. The constraint and
record work cleared two more poc files (``vectors.vhdl``, ``axi4stream.pkg``,
17 by the same per-file count).
Physical literals in generic clauses were one parse-level family in that
histogram: 15 poc refusals, all gone once a user-declared physical type is read.
Index-range attributes and index constraint lists were the next: the sortnet and
stat cores now parse, and poc's remaining refusals are the record constraints
``t(open)(Field(r), ...)`` of the AXI4 packages, the ``protected`` type, a
``context`` declaration and the packages' own unresolved names. What remains of
patmos' 99 is its argo wrappers, whose ``work.ocp`` and ``work.argo_types``
packages are not in the repository at all, plus the ``std_logic_misc`` gap above
(N51). The sweep also names the constructs still refused in RTL that no core in
the corpus happens to combine with a testbench, which is where the next finding
comes from.

The ported testcases were re-run after each change: 11 run scenarios and 4 cycle
traces (395, 612, 615 and 5621 cycles) pass against the native frontend.