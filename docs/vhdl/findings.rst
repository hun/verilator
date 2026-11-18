.. DESCRIPTION: Verilator: native VHDL frontend findings
   SPDX-FileCopyrightText: 2026-2026 Hannes Klas
   SPDX-License-Identifier: CC0-1.0

############################
Native VHDL frontend findings
############################

Status of the gaps found by running the in-tree VHDL frontend over the external
corpus in ``vhdl_references/corpus`` and over ``third_party``. Every finding
below was closed by a frontend-only change unless it says otherwise: no new AST
node, pass or runtime primitive was added to the V3 middle end.
:doc:`architecture` describes the frontend these findings are about.

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
    - an inout port
    - the frontend emitted no tristate driver shape, so the port reached the DFG
      undriven. Inout ports are now emitted as nets, ``'Z'`` is preserved, and
      conditional, selected and process drivers are lowered to the shape
      ``V3Tristate`` recognizes. A non-top port becomes an input plus
      ``__en``/``__out``; the top-level interface and the two-state conflict
      behavior are described in :doc:`review`
    - ``t_vhdl_inout``, ``t_vhdl_inout_bus``, ``t_vhdl_inout_proc``
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
  * - N93
    - an instance that associates the same port twice: ``port map (a => x, a => z)``,
      as a generated wrapper did while it was being written
    - the second association silently replaced the first, so the model was built from
      the wrong connection with no diagnostic, while GHDL refuses it. A whole port may
      now be associated once; disjoint slices or record elements of one port still
      combine, which is what the microwatt shims rely on
    - ``t_vhdl_port_dup_bad``
  * - N92
    - the asynchronous-reset flip-flop every synchronous design writes:
      ``process (rstn, clk) is if rstn = '0' then <reset> elsif rising_edge(clk) then
      <clocked> end if;``, as neorv32's CPU, its control unit and its frontend write
      them
    - the process was emitted as a level-sensitive always over both signals, with the
      clock edge tested inside the body, which is not a flip-flop as far as the
      scheduler is concerned: for a design large enough to split the block, the part
      that committed the registers lost the clock from its trigger set and no register
      ever updated (N90). The idiom is now recognised and emitted as
      ``always @(posedge clk or negedge rstn) if (!rstn) ... else ...``, so the clock is
      the block's sensitivity
    - ``t_vhdl_async_reset`` keeps the semantics; the discriminating case is the corpus's
      ``neorv32-cpu``, whose model fetched nothing before this change and now runs a
      12.2M-cycle program to the same four words the host and potato produce
  * - N91
    - a call of a function nested in another call's own actuals: neorv32's top selects
      its boot address with
      ``sel_suv_f(cond, base_io_bootrom_c, sel_suv_f(cond, BOOT_ADDR_CUSTOM, ...))``
    - the recursion guard marked a subprogram's body before its actuals were bound, so
      the inner call of the same function looked recursive and the constant that holds
      the result could not be folded, which then made the constant look unknown where a
      generate body passes it on as a generic. The body is marked once its actuals are
      bound, so nesting folds while a body that calls itself is still reported
    - ``t_vhdl_nested_call`` and ``t_vhdl_recursive_bad``
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
  * - N95
    - a record-member port association in a parent that does not import the
      package declaring the child's record type: ``i.x => a, i.y => b``
    - the record type was looked up only in the instantiating module's scope, so a
      parent that names the elements without importing the type got "record-member
      association on a non-record port". The lookup now falls back to the packages
      the child entity uses. Element associations on a constrained record work; an
      unconstrained record still needs a whole-record actual (N94)
    - ``t_vhdl_record_ind``
  * - N96
    - mixed ``unsigned``/``signed`` comparisons and an unsigned operand compared
      with a negative integer, as ``ieee.std_logic_arith``'s comparison functions
      define them
    - a mixed comparison was refused, and an integer was converted to the
      unsigned operand's width, so ``unsigned(x) = -1`` matched when ``x`` was all
      ones. ``std_logic_arith`` widens the unsigned operand by one bit and compares
      as signed, for an integer operand too, so a negative value never aliases the
      unsigned bit pattern
    - ``t_vhdl_arith_conv``; the corpus ``sns01/cmp*`` units lint
  * - N51
    - ``shl`` and ``shr`` from ``IEEE.std_logic_misc``
    - they are lowered as intrinsics, logical for both signednesses. The GHDL
      ``sns01``/``sns02`` cases pass
    - ``t_vhdl_arith_conv``
  * - N94
    - an unconstrained record port associated only by its elements:
      ``i.x => a, i.y => b``
    - the specialization only knew a whole record actual, so no element fixed
      the member ranges and the port was reported as an unconstrained type. Each
      association that names an element now fixes that element's range, and the
      declared record keeps its constrained elements, so an actual with a scalar
      member works too
    - ``t_vhdl_record_uncon``; the corpus ``comp05/inter02`` and
      ``comp05/inter03`` units pass
  * - N97
    - a record port whose declared subtype constrains one of its unconstrained
      elements: ``rec_out : out test_rec(vec_unbound(n-1 downto 0))``
    - a record mark with an unconstrained element was always treated as
      unconstrained, even when the port spec fixed that element, so the entity
      was parked as an unspecialized-port entity and reported. A port is now
      unconstrained only when its spec leaves at least one such element unnamed
    - ``t_vhdl_record_uncon``; the corpus ``issue2119`` unit passes
  * - N43
    - an aggregate that names part of its target in a choice range, as a
      concatenation operand: ``(t_residue'left downto DIVIDEND_BITS => '0') &
      unsigned(Dividend)``
    - with no target type, the aggregate's width comes from its choices, but a
      descending range was read as an ascending pair, so the span came out
      negative and the operand was refused. A choice now covers the elements
      between its bounds whichever way it names them, so the aggregate is that
      many elements wide and the concatenation supplies the rest of the word. An
      aggregate that really leaves a target element uncovered is still refused
    - ``t_vhdl_agg_choice``; poc-divider lints and passes its 8235 checks
  * - N98
    - an overloaded call with an integer actual and both integer and real
      declarations: ``ite(PIPELINED, STEPS, 0)`` in poc's ``utils``
    - the exact-match pass treated an integer actual as exact for a real
      formal, so both declarations matched and the call was reported ambiguous.
      An integer actual now matches only an integer formal exactly; a real
      formal still accepts it when it is the only candidate
    - ``t_vhdl_overload``; poc-divider lints and passes its 8235 checks
  * - N99
    - a character literal in an 8-bit vector context became its ASCII code:
      ``v and '1'`` yielded ``x"31"``
    - a logical operator combines a scalar character literal element-wise with
      the other operand (IEEE 1076-2008 9.2.2), so the literal keeps its logic
      value; any other operator still takes the character-code context an 8-bit
      operand gives it, which a string or ``character`` comparison needs
    - ``t_vhdl_char_lit_vec``
  * - N102
    - a negative shift count was the wrong shift: ``"1011" sll (-1)`` gave
      all zeros instead of the ``srl 1`` value
    - the count selects the direction (IEEE 1076-2008 9.2.4): a static
      negative count lowers as the opposite operator with its magnitude, and
      a dynamic one selects between the two shifted values on the count's
      sign. Rotates already handled it
    - ``t_vhdl_shift_neg``
  * - N100
    - a qualified predefined scalar was re-typed as an unsigned vector:
      ``integer'(-5) / 2`` folded to 2147483645 and ``integer'(-1) < 0``
      compared as constant-false unsigned
    - the qualified expression resolves its mark through the type table
      before the vector fallthrough, so a scalar mark keeps its scalar type
      (IEEE 1076-2008 9.3.6)
    - ``t_vhdl_qual_int``
  * - N101
    - a don't-care in a constant was compared as data: ``std_match(x, pat)``
      with ``pat`` a constant ``"1-0"`` never matched a pattern it should
    - the wildcard mask is also built from the lowered operands, whose
      don't-care bits are x bits (IEEE 1076-2008 16.8.3, 9.2.3), so a
      ``'-'`` that arrives through a constant wildcards the position it names
    - ``t_vhdl_std_match_const``
  * - N103
    - a run-time ``for`` loop re-read its bound every iteration: a loop whose
      body decremented the bound ran 2 iterations where VHDL runs 4
    - the non-static bound is captured into a temporary before the loop runs,
      so the trip count is the value the range had at loop entry (IEEE
      1076-2008 8.9)
    - ``t_vhdl_loop_bound``
  * - N104
    - the time-zero activation wrote signals with blocking assignments: a
      process that assigned ``a`` and then read it in its first activation
      saw the new value
    - a signal write is non-blocking even in the time-zero activation, so a
      read in the same activation still sees the old value until the delta
      (IEEE 1076-2008 10.5)
    - ``t_vhdl_init_delta``
  * - N105
    - ``process (all)`` was not sensitive to a signal read only in an
      assignment target's index: a RAM write whose address alone changed never
      happened
    - the read collector also walks each assignment target, collecting the
      index of an indexed name and the bounds of a slice but not the written
      name itself (IEEE 1076-2008 11.3 with 8.4)
    - ``t_vhdl_sens_lhs``
  * - N106
    - ``process (all)`` was not sensitive to a signal declared in the entity's
      declarative part: the value the process published stayed one event behind
      the design
    - an entity-level signal is a signal: only a variable declaration is
      classified as a variable, so the sensitivity pass no longer skips it
    - ``t_vhdl_sens_entity``
  * - N107
    - a bare ``wait;`` after a timed wait was dropped, and the process
      restarted: a testbench that stopped after three edges kept counting
    - a bare wait reached at run time sets a flag that gates every later
      activation, so the process suspends for good (IEEE 1076-2008 8.1); a
      wait of any other form inside a body is refused instead of dropped
    - ``t_vhdl_wait_bare``
  * - N108
    - a report message made only of logic characters was silently dropped,
      so ``report "0101" severity failure`` neither printed nor ended the run
    - a report message is a string whatever its characters are (IEEE
      1076-2008 8.3, 15.7): the parser keeps a string literal whole while it
      parses one, so the message prints. A process whose only stopper is a
      severity failure is a valid process: it runs once, at time zero
    - ``t_vhdl_report_bits``
  * - N109
    - statements after an ``assert ... severity failure`` still executed in
      the same pass: a second failure message printed, and an assignment
      after the failure took effect
    - severity failure ends the run at the statement (IEEE 1076-2008 8.3):
      nothing after an unconditional report is lowered, and the statements
      after a failing assert run only when it did not fail
    - ``t_vhdl_sev_stop``
  * - N111
    - ``abs`` on a ``numeric_std`` ``signed`` value was refused as
      ``abs of a non-integer value``
    - the absolute value compares and negates a signed vector in place, as
      ``numeric_std`` defines it (IEEE 1076-2008 Annex C)
    - ``t_vhdl_abs_signed``
  * - N110
    - legal forms refused: a based literal with an exponent (``16#FF#E2``),
      positional ``open``, an instantiation with no maps, ``for i in <subtype>
      generate``, ``view`` used as an identifier, a use clause naming one
      declaration, and ``'('`` as a character literal
    - each form is now parsed and modelled: the based literal scales by its
      exponent (15.5); an ``open`` actual takes the formal's default
      (6.5.7.1); an ``entity``-headed statement with no map is an
      instantiation (6.8); a generate range may name a discrete subtype
      (11.8); ``view`` is an ordinary identifier (15.10) while a view
      declaration stays refused; a use clause imports the one declaration it
      names (12.4); and ``'('`` stays a literal after a reserved word (15.6)
    - ``t_vhdl_legal_forms``
  * - N112
    - ``case`` completeness, duplicate and overlapping choices were not
      checked; a ``with ... select`` without ``others`` was accepted; a second
      ``others`` arm was silently dropped
    - every case and selected choice list is checked as it lowers: a value
      covered twice (which an overlapping range also is) and a second
      ``others`` arm are reported, and a list without ``others`` must cover
      every value the selector can take, an enumeration's literals, both
      values of a scalar, or all combinations of a vector up to 16 bits
      (IEEE 1076-2008 10.9, 11.5)
    - ``t_vhdl_case_choice_bad``
  * - N113
    - ``a nor b nor c`` and ``a nand b nand c`` without parentheses were
      accepted
    - a chain of logical operators of one precedence level without parentheses
      is refused unless every operator is the same associative one, so
      ``nand``/``nor`` chains and mixes such as ``a and b or c`` need
      parentheses (IEEE 1076-2008 9.1)
    - ``t_vhdl_nor_chain_bad``
  * - N114
    - illegal basic identifiers (leading, trailing or doubled underscore) and
      reserved words used as identifiers were accepted
    - the lexer checks the basic identifier spelling (IEEE 1076-2008 15.4.2),
      and a declared name may not be a reserved word (15.10): the shared name
      list, the design-unit, type, subtype, alias, component and loop-variable
      names are checked, while a reserved word that the grammar itself uses,
      such as an attribute name or `others`, stays legal
    - ``t_vhdl_ident_bad``; two inout tests used the reserved word `bus` as a
      signal name and were corrected
  * - N115
    - multiple drivers on an unresolved signal were accepted, and the model
      was last-write-wins
    - the emitted assignments are counted per bit position, since a composite
      signal's scalar subelements are separate signals: two statements that
      drive the same bit of an unresolved signal are reported (IEEE 1076-2008
      12.6.4), while element and record-member writes to one signal do not
      collide, and std_logic and the arrays built from it stay resolved
    - ``t_vhdl_driver_bad``
  * - N116
    - a port or generic map that mixed positional and named associations was
      accepted, and the named formals were then ignored
    - an association list is refused as soon as its form changes, so it is
      either all positional or all named (IEEE 1076-2008 6.5.7.2)
    - ``t_vhdl_assoc_mix_bad``
  * - N117
    - a port actual of the wrong width was silently resized, and no port
      type conformance was checked
    - the whole-port association compares the actual's type with the formal's
      before connecting it: a width mismatch, or an element-kind mismatch when
      the actual is a designator of a known type, is reported (IEEE 1076-2008
      6.5.2.2)
    - ``t_vhdl_port_conform_bad``
  * - N118
    - integer literals above 2**31-1 wrapped silently (``4000000000`` became
      -294967296), and a constant was never checked against its declared
      subtype range
    - an integer literal that does not fit the 32-bit integer type is refused
      where it is parsed (IEEE 1076-2008 15.5; the minimum, written as 2**31
      under a unary minus, is allowed), and an integer constant's value is
      checked against a declared subtype of ``integer`` (5.4). A physical
      type, modelled as an integer, carries no such bounds and is skipped
    - ``t_vhdl_int_lit_bad``, ``t_vhdl_const_range_bad``
  * - N119
    - arithmetic on ``std_logic_vector`` was accepted with no visible ``"+"``
      overload, modelled as unsigned
    - an arithmetic operator on a std_logic_vector is refused unless
      std_logic_unsigned, std_logic_arith or std_logic_signed is visible,
      since the predefined operators have bit and boolean elements (IEEE
      1076-2008 9.1). Shifts stay legal: std_logic_1164 (2008) declares the
      std_logic_vector ones, which the shift test pins against GHDL
    - ``t_vhdl_slv_arith_bad``
  * - N120
    - physical objects at run time (``variable d : time``, user physical
      types) were accepted and modelled as integers
    - the object-type gate also refuses a physical value at run time, since a
      physical value is a count of its step unit only while elaborating
      (subset 5.14); a delay and a static fold still use the 64-bit count
    - ``t_vhdl_phys_obj_unsup``
  * - N121
    - a ``wait`` inside a process with a sensitivity list was silently
      dropped, and the process ran on both clock edges instead
    - a wait statement inside a lowered body is refused unless it is a bare
      wait whose process suspends on it (IEEE 1076-2008 10.2), so the
      sensitized form is a clean refusal
    - ``t_vhdl_wait_sens_bad``
  * - N122
    - a sensitivity item that is not a bare name was silently turned into
      ``process(all)``, changing the semantics of an exact list
    - the element is no longer widened to ``process(all)``; it is its own
      sensitivity item, so the process runs on that element's events only
      (N127, which replaces the temporary refusal)
    - ``t_vhdl_sens_elem``
  * - N123
    - ``T'val`` out of range silently yielded a value that matched no literal,
      and the predefined scalar marks refused most attributes (``bit'high``,
      ``std_logic'left``)
    - ``'val`` outside ``T'low``..``T'high`` is reported, both where the
      attribute lowers and where the static evaluator folds it, and the
      predefined logic marks now answer the discrete attribute set
      (IEEE 1076-2008 16.2.2)
    - ``t_vhdl_attr_scalar``, ``t_vhdl_attr_val_bad``
  * - N124
    - division, ``mod`` and ``rem`` by zero silently yielded 0
    - a static zero divisor is reported where the operator lowers (IEEE
      1076-2008 9.1; subset row 4.5), and a dynamic one gets a module-level
      check that reports and ends the run when it reaches zero, the same
      run-time check the enumeration bounds use
    - ``t_vhdl_div_zero_bad``
  * - N125
    - an uninitialized integer object starts at 0 rather than ``T'left``
      (-2**31)
    - zero is kept as the documented starting value, like the logic-value
      collapse: `vhdl_synth_subset.md` 5.1 now says so, arguing that emitting
      ``T'left`` would surprise the zero-initialized C++/SV user and that a
      synthesizable design initializes or resets its state
    - documented; no test
  * - N126
    - ``vhdl_synth_subset.md`` disagreed with the implementation in both
      directions: rows marked D/X were implemented, rows marked S were hollow
    - the tier table now matches the shipped frontend: the implemented forms
      moved to S (deferred constants, entity statement part, conditional
      sequential assignment, ``unaffected``, matching ordering, ``std_match``,
      ``'element``, ``integer_vector``, generic package instantiation, timed
      waits), the flattened-label and missing-signal rows say what the model
      really does, and ``boolean'image`` returns lower case per IEEE 1076-2008
      16.2.2 with ``t_vhdl_image_attr`` re-pinned
    - ``t_vhdl_image_attr``
  * - N127
    - a sensitivity element that was not a bare name was refused, so a process
      written as ``process(rec.field, v(i))`` (neorv32's CPU blocks) could not
      be lowered
    - a selected, indexed or sliced name denotes a subelement signal: its
      lowered designator is the sensitivity item, so the process runs when that
      element changes and not when another element of the same signal does
      (IEEE 1076-2008 11.3). The count of items also drives the edge/level
      decision, as for bare names
    - ``t_vhdl_sens_elem``
  * - N128
    - a range-choice aggregate as an actual of an unconstrained formal, e.g.
      ``Cnt + ite(Cnt(BITS) = '0', (Cnt'range => '1'), x)`` (poc's
      ``arith_Counter_Free`` and ``misc_StrobeGenerator``)
    - an aggregate has no type of its own, so overload selection matched every
      declaration that takes its number of actuals and took the first, which
      for poc's ``ite`` set was the ``boolean`` one. The typed operand of an
      operator now gives a call or aggregate beside it its context
      (IEEE 1076-2008 9.1), the context picks the declaration whose return type
      it needs, and a choice that is an object's ``'range`` contributes its
      bounds when the aggregate derives its width
    - ``t_vhdl_agg_choice_actual``
  * - N129
    - a type whose element range names an object of the same declarative part
      (``type row_t is array (natural range <>) of std_logic_vector(data'range)``)
      or a subprogram local type bounded by a local constant (open-logic's
      ``ppcOr``)
    - a part registers its types before its objects exist, so a ``'range``
      prefix that the part declares itself was unknown, and a subprogram
      registered all its local types before binding the constants they name.
      A type that waits for an object is deferred until the part's ports and
      objects are in scope, and a subprogram registers its local types in
      source order, so each sees the constants before it. A package also
      publishes the subprograms its own ``use`` clauses made visible, as it
      already published their types and constants, so ``ppcOr`` can reach
      ``log2ceil`` when only the logic package is imported
    - ``t_vhdl_type_bound_object``
  * - N130
    - a null slice as an assignment target, e.g. the pipeline shift
      ``p(2 to N) <= p(1 to N-1)`` with ``N=1`` (open-logic's arbiter)
    - a slice whose bounds cross is the null array (IEEE 1076-2008 5.3.2), so the
      statement assigns nothing and is dropped, as ``unaffected`` already is. A
      null slice used as a value is still reported, since the model has no
      zero-width value
    - ``t_vhdl_slice_empty``
  * - N131
    - a folded function that rewrites an array local could exhaust memory before
      the element-write budget stopped it (open-logic's ``ppcOr``, 30 GB at
      ``3x8`` iterations)
    - each element write rebuilds the local's whole value, and every read of the
      local copies that value into the rebuild, so the budget has to follow the
      copied tree's size and not only the value's width. The budget now adds the
      node count, so a body whose value grows per write is refused with the
      existing diagnostic instead of being folded
    - ``t_vhdl_fn_write_cost_bad``
  * - N132
    - a folded function that rewrites an array local could exhaust memory, and a
      loop bound that names a local constant initialized by a call was refused
      (open-logic's ``ppcOr``, which the priority arbiter calls)
    - when the call has a statement sink, an array local is a real variable, so
      an element write is one statement instead of a rebuild of the whole value
      that copies it on every read; the budget still bounds the value-driven
      fold, and a write under a condition that does not fold is refused because
      the evaluator lowers both paths of such a condition. A loop bound now folds
      through a constant's initializer
    - ``t_vhdl_fn_array_local``, ``t_vhdl_fn_cond_array_bad``
  * - N133
    - ``v(v'high downto 1)`` was refused after ``v := ppcOr(inp)``: the tracked
      value replaced the name inside the type attribute
    - a type attribute such as ``'high`` or ``'range`` describes the declared
      type, not the tracked value, so its prefix keeps its name when the tracked
      values are substituted
    - ``t_vhdl_var_attr_slice``
  * - N134
    - a string generic bound to an empty string, or to a string of logic
      characters, was refused: open-logic passes `InitString_g => ""` and forwards
      it, and its RAM blocks then lost the instance entirely
    - the generic binder treated an empty text as "no value" and only looked at
      `Expr::STRING_LIT`, while a string of logic characters parses as a bit-string
      literal. The binder now records the text for every string generic (empty
      included) and reads both literal forms; a comparison of a string generic with
      a literal folds by text, so `INIT = ""` selects a generate branch without
      needing a packed value
    - ``t_vhdl_string_generic``
  * - N135
    - an unconstrained vector generic (`Polynomial_g : std_logic_vector`), which
      open-logic's PRBS declares and binds to a bit-string literal
    - such a generic takes its bounds from the actual at each instance, as a port
      of the same type does, and registers as a constant of the actual's type. A
      conversion operand that is a vector designator no longer folds to an integer
      first: `to_integer(unsigned(v))` saw a non-vector value when `v` was a bound
      generic
    - ``t_vhdl_vector_generic``

____
Open
____

The two findings left here need more than a frontend change: N15 wants
``AstModule::isTop()`` from the middle end, and N67 is the simulation-only
subset. The conformance review against IEEE 1076-2008 with GHDL and nvc as the
reference simulators (findings N99..N127) is closed; every one of those is in
the Closed table with the test that pins it.

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
  * - N67
    - the simulation-only subset: a ``line`` variable *written* by a debug
      procedure through ``ieee.std_logic_textio`` (bit-serial; reading a file into
      an image is N76), poc's ``protected`` type definition, and
      neorv32's trace file name ``"neorv32_trace" & natural'image(HART_ID) & ".log"``
    - none of them has a synthesizable meaning: an access type with file operations,
      a semaphore built for shared variables, and a string constant feeding a trace
      logger. A cycle model cannot represent them, so the refusal is deliberate; a
      design that only uses them in a simulation branch keeps everything else
___________
Verification
___________

Each run was checked with the full VHDL suite (``test_regress``,
``t_vhdl_*.py``): 239 tests, no failures. The conformance review added 28 tests
to the original 211, one or more per closed finding below, including the
negative tests the new diagnostics required.

The corpus lints and runs clean: 20 designs, no lint errors, and every run
passes, including the harnesses added for the rpu ALU, the rpu register set, the
t65 CPU and microwatt's ``soc_reset`` and ``wishbone_bram_wrapper``, and neorv32's
whole SoC, which boots a generated image from its instruction memory and reports
its results over GPIO. The last of
those runs ``fpga/main_bram.vhdl`` unmodified, its ``RAM_INIT_FILE`` read at
elaboration from a checked-in image. hdl-modules' ``ring_buffer_write_simple``
runs too, its width checks folded at elaboration (N86).

The whole neorv32 core (``rtl/core/*.vhd`` with ``neorv32_top`` as the top) lints with
no errors, and its CPU runs the same freestanding C program as potato's SoC (N92): the
corpus harness feeds it the image, acts as its memory and checks the same four result
words, over 12.2M cycles, and the SoC top runs a C program over the same bus and its
GPIO peripheral.

Two simulators are used as ground truth: GHDL, and nvc (``nvc --std=2008``)
whenever GHDL and the frontend disagree. nvc settled the value of a toggle
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
traces (395, 612, 615 and 5621 cycles) pass against the frontend.