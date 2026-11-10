<!-- SPDX-FileCopyrightText: 2026 Hannes Klas
     SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0 -->

# VHDL synthesis subset for Verilator

**Status:** feature-set definition for the `--language vhdl` frontend. This is a
planning document. It is not user documentation and must not be linked from
`docs/`.

**Scope:** this document defines *what* the VHDL frontend accepts and how it must
behave. The architecture, work packages, and implementation sequencing stay in
[VHDL_FRONTEND_PLAN.md](VHDL_FRONTEND_PLAN.md). If the two disagree on feature
scope, this document wins; the plan wins for implementation order.

**Reference:** AMD Vivado Design Suite User Guide: Synthesis, UG901 (v2026.1,
2026-07-08). The feature tables below are derived from Chapters 6 (VHDL Support,
pp. 183-216), 7 (VHDL-2008 Language Support, pp. 217-228), 8 (VHDL-2019 Language
Support, pp. 229-239), 11 (Mixed Language Support, pp. 303-307), and Chapter 3
for synthesis attributes. Vivado is a synthesis tool, so its support table is a
statement about what it can map to hardware, not a full conformance statement.
Verilator additionally has to simulate what it accepts; the semantic decisions in
Section 5 make that difference explicit.

## 1. Purpose

The frontend targets the synthesizable VHDL subset that real FPGA designs use in
practice. UG901 is the representative reference because it is the subset AMD
documents for Vivado synthesis. The subset is deliberately narrower than the
VHDL-2008 standard:

- Verification-only constructs (access types, protected types, file I/O,
  textio, allocators, `force`/`release`) have no hardware meaning and are
  rejected with a source-located diagnostic.
- Simulation-only semantics that Verilator cannot reproduce (full 9-state
  resolution, delta-cycle ordering across processes, inertial delays) are either
  collapsed to the value model in Section 5.1 or rejected. They are never silently
  approximated.
- Anything that is not in this document is not part of the subset. A construct
  that is not listed is rejected with an `Unsupported:` diagnostic, never
  accepted by accident. Unknown constructs must not produce a wrong model.

## 2. Support tiers

| Tier | Meaning |
|---|---|
| `S` | Supported. Part of the normative subset; must accept and model correctly. |
| `S*` | Supported with a documented semantic divergence or dependency. See the note and Section 5. |
| `D` | Deferred. Accepted only when implemented; until then a clean `Unsupported:` error. |
| `O` | Optional. Not needed for the synthesizable subset; implement only with a concrete user need. |
| `X` | Excluded. Always a clean `Unsupported:` error. |
| `I` | Ignored. Parsed and accepted, but has no effect on the generated model. |

Vivado column: `Y` = supported, `P` = supported with restrictions, `N` = not
supported, `-` = not stated in UG901.

**Supported set at a glance.** VHDL-2008 synthesizable RTL: entities and
architectures, packages, components and direct instantiation, constant and type
generics, logic and numeric types, records, one-dimensional arrays (including
arrays of arrays), processes and concurrent assignments, generate statements,
static loops, pure functions and procedures, `std_logic_1164` and `numeric_std`,
and the standard attributes. Deferred: configurations, dynamic loops, waits
beyond a top-level clock edge, matching ordering operators, multi-dimensional
arrays, generic packages and generic subprograms, and VHDL-2019. Excluded: access,
protected, and file types; real objects; timing constructs; fixed-point and
floating-point packages; external names; and user-defined resolution functions.

The tiers are about the target feature set, not about current implementation
status. The current state is tracked in `handoff.md` and the plan.

## 3. Dialect and source handling

| Item | Vivado | Tier | Notes |
|---|---|---|---|
| VHDL-2008 as the default dialect | Y | S | GHDL is invoked with `--std=08`; all new tests use VHDL-2008. |
| VHDL-2002 source | Y | O | A `--std` selector is deferred. |
| VHDL-93 source | - | O | Same selector; a few VHDL-2008 reserved words can break old identifiers. |
| VHDL-2019 source | Y (subset) | D | See Section 5.12. VHDL-2019 is not the baseline. |
| `.vhd` / `.vhdl` extension detection; `--language vhdl` | - | S | Explicit `--language` wins over extension. |
| `--top-module <entity>` | - | S | Selects the top VHDL entity; error with a spell-check suggestion if missing. |
| Mixed VHDL/SV sources in one run | Y | X | Vivado accepts mixed files but restricts mixing to design-unit instantiation. Verilator rejects mixed design sources until the mixed-language milestone. Section 5.13. |
| VHDL design units are order independent | Y | S | Collect per file, analyze/elaborate after all files are read. |
| `library` / `use` clauses | Y | S | `work` is the default; `ieee` is built in. |
| `context` declaration and `context` reference (VHDL-2008) | Y | S | Resolved during analysis; no lowering effect. |
| Source locations in diagnostics | - | S | Every diagnostic carries VHDL `file:line:column`. |
| VHDL identifiers case-insensitive for lookup | Y | S | Keep original spelling on emitted names; use a folded key for scope lookup. |
| Extended identifiers (`\name\`) | - | O | Requires the same spelling preservation as Verilog boundary names. |
| Leading underscore in identifiers | N | X | VHDL does not allow it; extended identifiers are the escape form. |
| Synthesis attributes from RTL | Y | I | Section 5.10. |

## 4. Feature catalog

### 4.1 Design units, libraries, hierarchy

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| `entity` + one architecture | Y | S | An entity with no reachable architecture is an error. |
| Architecture declarative part | Y | S | Signals, constants, types, subtypes, aliases, components, attributes, subprograms. |
| Multiple architectures for one entity | Y | S* | Use the last analyzed architecture as the default; an explicit configuration selects another. |
| Entity statement part (passive statements) | N | X | UG901 Table 15. |
| `package` and `package body` | Y | S | Types, subtypes, constants, subprograms, component declarations. |
| Deferred constant (`constant` in package, value in body) | N | X | UG901 Table 15. |
| `context` declaration/reference | Y | S | VHDL-2008. |
| Component declaration | Y | S | Ports and generics are validated against the bound entity. |
| Component instantiation with default binding | Y | S | Search `work` first, then the configured libraries. |
| Direct entity instantiation (`entity work.e(a)`) | Y | S | Preferred form for VHDL-to-VHDL. |
| Recursive component instantiation | Y | D | Requires a static elaboration/recursion-depth bound. |
| Configuration declaration (`for all : c use entity work.e(a);`) | Y | D | UG901 supports only the `all` clause for the instance list. |
| Configuration with a specific instance list or `others` | N | X | UG901 Table 15. |
| Top-level configuration name as `--top` | Y | D | Same lowering as a configuration-selected architecture. |
| `disconnect` specification | N | X | UG901 Table 15. |
| `--work` library selection | - | S | Default is `work`. |
| Library search through `-y` / `-v` | - | D | Deferred: the current auto-load path still uses the SystemVerilog parser. A VHDL run never auto-loads SystemVerilog sources. |

### 4.2 Ports, generics, and interfaces

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Port modes `in`, `out`, `inout`, `buffer` | Y | S | `buffer` is lowered as an output that is also readable internally. `inout` depends on the tristate/DFG fix; until then it is a clean `Unsupported` when it cannot be modeled. |
| `linkage` port mode | N | X | UG901 Table 15. |
| Scalar port types (`bit`, `boolean`, `std_logic`, `std_ulogic`, enums, integer subtypes) | Y | S | |
| Vector/array port types (`bit_vector`, `std_logic_vector`, `std_ulogic_vector`, `unsigned`, `signed`) | Y | S | One-dimensional. |
| Record ports | - | S | Record types are supported; UG901 does not explicitly list record ports. Members are packed in declaration order; member selection and aggregates work. |
| Unconstrained array/record ports | Y | S* | Specialized per instantiation from the actual. UG901 recommends against unconstrained ports, especially on the top level; Verilator reports an unconstrained top-level port as no-top rather than inventing a width. |
| Multi-dimensional array ports | N | X | UG901: array types of more than one dimension are not accepted as ports. |
| Named port maps | Y | S | |
| Positional port maps | Y | S | |
| `open` port associations | Y | S | Outputs may be left open; inputs with `open` require a default and are an error otherwise. |
| Expressions in port maps (VHDL-2008) | Y | S | Inputs may be expressions, function calls, or conversions; outputs require a writable name, slice, index, or aggregate target. |
| Individual element/slice associations (`p(3 downto 0) => sig`) | - | S | Inputs are packed into one concatenation; outputs are driven through a temporary. |
| Partial connections with `open` (VHDL-2019) | Y | O | Section 5.12. |
| Generic types: `integer`, `boolean`, `string`, `real` | Y | S | `real` is static-only and folded at elaboration. |
| Generic defaults | Y | S | |
| Named and positional generic maps | Y | S | |
| Generic referencing another generic | Y (2008) | S | |
| Generic type parameters (VHDL-2008) | Y | S | Per-binding type specialization; a generic type is monomorphized at each binding. |
| Generic subprogram parameters (VHDL-2008) | Y | O | Requires a function-pointer-like lowering or full specialization. |
| Generic package instantiation (VHDL-2008) | Y | D | A package instance must be bound to the actual generic values; ignoring the instance is not acceptable once its types are used. |
| Array-element override in a generic map (`g(1) => x`) | N | X | UG901 says use an aggregate actual (`g => (x, y)`). |
| `open` in a generic map | - | O | Uses the formal default. |
| Unconstrained top-level entity port | N (recommended against) | X | No single width exists; report a clear no-top diagnostic. |

### 4.3 Types and subtypes

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| `bit`, `boolean` | Y | S | 1-bit logic; `false` = 0, `true` = 1. |
| `integer`, `natural`, `positive` | Y | S | 32-bit signed two's complement. Static range checks only (Section 5.4). |
| User-defined integer subtypes with constant bounds | Y | S | Range is checked statically; no runtime range exception. |
| `real` | P | S* | Only in static expressions/generics for constant calculations. A signal/variable/port of type `real` is rejected. |
| `time` and other physical types | P | S* | Only in static constant calculations. Physical literals are folded; runtime physical objects are rejected. |
| `character`, `string` | Y | S* | `string` is an array of `character` (8-bit elements). String generics and string literals are supported; non-logic character values are deferred (Section 5.11). |
| `std_logic`, `std_ulogic` | Y | S* | 9 values are collapsed to the Verilator 4-value constant model; see Section 5.1. |
| `std_logic_vector`, `std_ulogic_vector` | Y | S* | Conservative arithmetic is not applied; VHDL operator semantics are lowered explicitly. |
| `unsigned`, `signed` (`numeric_std`) | Y | S | Same underlying logic vector with explicit signedness. |
| `unsigned`, `signed` (`std_logic_arith`) | Y | D | Legacy Synopsys package; conversions are partially implemented today. |
| `numeric_bit` types | Y | D | `unsigned`/`signed` based on `bit`. |
| `boolean_vector`, `integer_vector` (VHDL-2008) | Y | D | |
| User-defined enumeration types | Y | S | Literal order defines `'pos`; explicit encoding follows declaration order. |
| Records | Y | S | Nested records, constants, member selection, positional/named aggregates. UG901: a record field cannot carry an attribute. |
| One-dimensional arrays | Y | S | Including arrays of vectors, arrays of records, and arrays of arrays. |
| Constrained array subtypes | Y | S | Full index range; ascending and descending. |
| Unconstrained array types and subtypes | Y (2008) | S | Elements constrain from the object or port actual. |
| Unconstrained element types (VHDL-2008) | Y | D | Array-of-unconstrained-array objects, `type T is array (natural range <>) of std_logic_vector`. |
| Multi-dimensional arrays (internal objects) | Y | D | UG901 recommends at most three dimensions; it does not restrict dimensions. |
| Fully constrained arrays in every dimension | Y | S | UG901's baseline requirement; unconstrained dimensions are only accepted where the object or port supplies them. |
| `access` types and allocators (`new`) | N | X | UG901 Table 15. |
| Protected types | Y (2008) | X | Verification-only; no synthesis/hardware meaning. |
| File types | N | X | |
| Type conversion (`T(expr)`) | Y | S | Includes array/vector conversions and numeric conversions. |
| Qualified expressions (`T'(expr)`) | Y | S | UG901 supports them for accepted predefined attributes; Verilator accepts them generally. |
| Enumeration literal | Y | S | |
| Integer width (32-bit default) | Y | S | UG901 default is 32 bits; there is no width option. VHDL-2019 switches to 64 bits. |
| 64-bit `integer` (VHDL-2019) | Y | O | Deferred; the baseline remains 32 bits. |

### 4.4 Objects and declarations

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Signal declaration | Y | S | Architecture and block declarative regions. Not `register`/`bus` kinds. |
| Variable declaration | Y | S | Process and subprogram declarative regions. |
| Constant declaration | Y | S | Any declarative region. |
| Shared variable | Y | S* | Modeled as a variable. Used by RAM coding styles. GHDL with `--std=08` rejects a non-protected shared variable unless `-frelaxed` is passed (or a VHDL-93 analysis mode is used); the frontend invocation must make that explicit. |
| Object alias (signal, variable, constant, slice) | - | S | Alias is VHDL-93; aliases of slices and constants work. |
| Signal declaration initializer | Y | S | Elaboration-time value; also a power-up value in the model. |
| Initializer from a static function call | Y | S | Folded during elaboration when the function is static. |
| Deferred constant | N | X | |
| Signal kind `register` / `bus` | N | X | UG901 Table 15. |
| Attribute declaration | Y (some) | I | Accepted; unrecognized attributes are skipped. |
| Attribute specification | Y (some) | I | Accepted; synthesis attributes have no effect. |
| Component declaration | Y | S | |
| Subprogram declaration/body | Y | S | Section 4.8. |
| Package-level signal declaration | - | O | Shared state across the design; only if a real design needs it. |
| Object name with a leading underscore | N | X | |

### 4.5 Expressions and operators

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Logical `and`, `or`, `nand`, `nor`, `xor`, `xnor`, `not` | Y | S | Scalar, vector, and matching element widths. |
| Unary logical reductions (`and v`, `or v`, `xor v`, ...) | Y (2008) | S | Also `*_reduce` from `std_logic_misc` (Section 4.9). |
| Mixed array/scalar logical operands | Y (2008) | S | Scalar is replicated across the array. |
| Relational `=`, `/=`, `<`, `<=`, `>`, `>=` | Y | S | VHDL result type is `boolean`. |
| Matching equality/inequality `?=`, `?/=` | Y (2008) | S | Don't-care positions in literals; no wildcard means same-value equality. |
| Matching ordering `?<`, `?<=`, `?>`, `?>=` | Y (2008) | D | Ordering forms of the matching operators. |
| Explicit condition operator `??` | Y (2008) | S | |
| Implicit scalar condition (`if s then`, `s` is `std_logic`) | - | S* | Compatibility extension; standard VHDL requires `??`. Accepted only for scalar logic types. |
| Concatenation `&` | Y | S | Arrays and scalars; result width is the sum. |
| `+`, `-`, `*` | Y | S | |
| `/` | P | S* | UG901 restricts the right operand to a constant power of two, or both operands constant. Verilator lowers VHDL division generally; a zero divisor is a runtime error. |
| `mod`, `rem` | P | S* | UG901 restricts the right operand to a constant power of two. Verilator applies VHDL sign rules, which differ from the SV `%` operator. |
| `**` | P | S* | UG901 restricts the left operand to 2. Static exponents and non-negative integer exponents are in subset; negative or real exponents are not. |
| `abs` | Y | S | |
| Unary `+`, `-` | Y | S | |
| Shifts `sll`, `srl`, `sla`, `sra` | Y | S | VHDL fill rules, including arithmetic fill for `sla`/`sra`. |
| Rotates `rol`, `ror` | Y | S | |
| `maximum`, `minimum` | Y (2008) | S | |
| Integer literals (decimal) | Y | S | |
| Based literals (`16#FF#`) | Y | S | |
| Bit-string literals (`B"..."`, `O"..."`, `X"..."`) | Y | S | |
| Enhanced bit-string literals (VHDL-2008) | Y | D | Signed/unsigned and length-prefixed forms. |
| String and character literals | Y | S* | String literals are supported; non-logic character contexts are deferred (Section 5.11). |
| Enumeration literals | Y | S | |
| Physical literals | P | S* | Folded in static contexts only; rejected in runtime expressions. UG901 simply ignores physical literals. |
| Record aggregates | Y | S | Positional, named, `others`. |
| Array aggregates | Y | S | Positional, named, range, `others`, and shared choices. |
| Array/record aggregate assignment targets | - | S | `(a, b) <= value`; first target is most significant. |
| Indexing and slicing | Y | S | Dynamic indices and non-static slices with statically known width. |
| Function calls | Y | S | |
| Operator-symbol calls (`"or"(a)`) | Y | S | |
| Qualified expressions | Y | S | |
| Type conversions | Y | S | |
| Allocators | N | X | |
| Static expressions | Y | S | Folded during elaboration, including ranges, generic values, and attributes. |
| `others` in aggregates | Y | S | |
| `unaffected` | Y | D | VHDL-2008 assignment value. |
| `open` | Y | S | Port/generic association. |
| Null array / zero-width array | - | X | Rejected cleanly; no zero-width hardware object. |

### 4.6 Sequential statements

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Signal assignment `<=` | Y | S | End-of-delta update; lowered to `AstAssignDly` in a process. |
| Variable assignment `:=` | Y | S | Immediate update; lowered to `AstAssign`. |
| `if` / `elsif` / `else` | Y | S | Condition is `boolean`; `??` or the scalar compatibility extension applies. |
| `case` | Y | S | Must be complete; `others` handles the remainder. |
| `case?` (VHDL-2008 matching case) | Y | D | Explicit don't-care positions. |
| `select?` (VHDL-2008 matching select) | Y | D | Concurrent form; see Section 4.7. |
| `for ... loop` with static bounds | Y | S | Unrolled during elaboration when bounds are static; otherwise see the next row. |
| `for ... loop` with non-static bounds; `while ... loop`; bare `loop` | - | D | Lowered to `AstLoop`/`AstLoopTest` without relying on `V3LinkJump`. |
| `next`, `exit` | Y | S | In static loops first; dynamic loops with the loop lowering. |
| `return` | Y | S | Function result and early procedure return. |
| `null` | Y | S | No effect. |
| `wait until <clock edge>` | Y | S | Only as a top-level wait in a process with no sensitivity list; statements before the wait re-execute after each wakeup. |
| `wait until <general boolean condition>` | Y | D | A non-clock condition needs a real event/level lowering; not in the baseline. |
| `wait on <signal> until <condition>` | Y | D | UG901 supports one signal in both the list and the condition; multiple waits are not supported. |
| `wait for <time>` | N | X | UG901 does not support it. Deferred behind `--timing` would reintroduce non-synthesis semantics; not in the subset. |
| Multiple wait statements in one process | N | X | |
| Wait statement together with a sensitivity list | N | X | |
| Wait-based latch | N | X | UG901 explicitly disallows latch descriptions from waits. |
| `assert` | P | S* | Static conditions are evaluated at elaboration. Runtime assertions are optional (Section 5.6). |
| `report` | - | I | No effect, matching synthesis. |
| Procedure call statement | Y | S | |
| Conditional sequential signal/variable assignment (VHDL-2008) | Y | D | `sig <= a when c else b;` |
| Delay/`after` on a signal assignment | P (delay ignored) | X | Verilator rejects a nonzero delay unless a future `--timing` mode is enabled; silently ignoring it would change simulation behavior. |
| `transport`, `inertial`, `reject` | N | X | |
| `force`, `release` | - | X | Verification-only, no hardware meaning. |
| Conditional expression / conditional return (VHDL-2019) | Y | O | Section 5.12. |

### 4.7 Concurrent statements and processes

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Process with an explicit sensitivity list | Y | S* | The list is honored exactly. Unlike Vivado synthesis, Verilator does not auto-add missing signals; a missing signal is a real simulation difference and should be diagnosed when detectable. `process(all)` is the supported way to ask for inference. |
| `process(all)` (VHDL-2008) | Y | S | Sensitivity is the set of signals read by the lowered body; process variables are excluded. A clock edge inside `process(all)` is deferred. |
| Process with a top-level clock-edge wait | Y | S | `wait until rising_edge(clk)`, `falling_edge`, and the `'event` form; statements before the wait re-execute after each wakeup. |
| Process with a sensitivity list that contains a clock plus asynchronous controls | Y | S | Async reset/set branch before the clock edge, synchronous logic inside it. |
| Clock idioms `rising_edge` / `falling_edge` | Y | S | Requires a `std_logic`/`std_ulogic` argument. |
| `sig'event and sig = '1'` clock idiom | Y | S | Normalized to the same edge form. |
| Multiple clocks in one process, or clocked and unclocked statements sharing state | - | D | Split into disjoint `always` blocks only when targets and process variables are provably disjoint; otherwise a clean `Unsupported`. |
| Simple concurrent signal assignment | Y | S | |
| Conditional concurrent signal assignment (`when`/`else`) | Y | S | |
| Selected concurrent signal assignment (`with`/`select`) | Y | S | |
| Selected matching assignment (`select?`, VHDL-2008) | Y | D | |
| Concurrent procedure call | Y | D | |
| Concurrent assertion statement | P | D | UG901 ignores it; Verilator may lower it to an assertion if a severity policy exists. |
| `for ... generate` with constant bounds | Y | S | Unrolled during elaboration. |
| `if ... generate` with a static condition | Y | S | |
| `elsif ... generate` / `else ... generate` (VHDL-2008) | Y | S | |
| `case ... generate` (VHDL-2008) | Y | D | |
| Generate declarative part | Y | S | Local signals, constants, types, and instances. |
| Generate hierarchy / labels | Y | S | Labels become hierarchy components and VCD scopes. |
| Block statement | Y (2008) | S | Flattened with a unique scope suffix; local declarations and nested concurrent statements work. |
| Block statement with port/generic/guard header | - | D | Guarded blocks and `disconnect` are excluded. |
| Concurrent signal assignment `after`, `transport`, `guarded`, waveform | N | X | UG901 Table 15. |
| Concurrent assignment `unaffected` | Y | D | VHDL-2008. |
| Latch inferred from an incomplete process | Y | S* | Modeled as level-sensitive storage. Verilator may warn because latches are usually unintentional; the warning must not reject the design. |
| Combinational loop | - | S* | Modeled as written; a loop through no state is a simulation artifact, not a frontend error. |

### 4.8 Subprograms

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Pure function | Y | S | Inlined for expressions; unconstrained formals take the actual's type. |
| Impure function | Y | D | Only if it reads a signal or shared state; requires a non-expression lowering. |
| Procedure with `in`, `out`, `inout` parameters | Y | S | Inlined; out/inout actuals must be writable names, slices, indices, or aggregates. |
| Parameter defaults | Y | S | |
| Named parameter association | Y | S | |
| Nested subprograms | - | S | Capture the enclosing declarative scope. |
| Unconstrained formal parameters | Y | S | Element/array bounds come from the actual. |
| Recursive functions | Y | D | Requires a real call mechanism rather than expression inlining; recursion in static functions can be folded at elaboration. |
| Signal parameters | - | D | `signal` class parameters need an alias/actual-signal binding. |
| Generic subprogram parameters (VHDL-2008) | Y | O | |
| Subprogram instantiation (VHDL-2008) | Y | O | |
| Resolution functions | P | X | UG901 supports only the resolution functions in `std_logic_1164`. User-defined resolution is deferred with multi-driver support. |
| `foreign` attribute / black-box subprograms | - | X | |

### 4.9 Packages and standard libraries

| Package | Vivado | Tier | Notes |
|---|---|---|---|
| `standard` | Y | S | `bit`, `boolean`, `integer`, `natural`, `positive`, `real`, `time`, `character`, `string`, `severity_level`. File types parse but file operations are excluded. |
| `ieee.std_logic_1164` | Y | S | Types, logic operators, `rising_edge`, `falling_edge`, `to_stdlogicvector`, `to_stdulogicvector`, `to_bitvector`, `to_stdulogic`, `to_bit`. |
| `std_logic_1164` `to_X01`, `to_X01Z`, `to_UX01` | - | O | Constant-level mapping; runtime values are already 0/1. `to_X01Z` needs tristate support for Z. |
| `std_logic_1164` `is_x` | - | X | A 2-state runtime cannot answer this faithfully for runtime signals. |
| `ieee.numeric_std` | Y | S | `unsigned`/`signed`, arithmetic, comparison, shifts/rotates, `to_integer`, `to_unsigned`, `to_signed`, `resize`, `shift_left`, `shift_right`, `rotate_left`, `rotate_right`, `minimum`, `maximum`. Division/modulus/remainder/exponentiation follow VHDL semantics. |
| `numeric_std` `std_match` | - | D | Useful for matching operators; works on 0/1/`-`. |
| `numeric_std` `to_01` | - | O | Constant-level mapping; runtime has no X to map. |
| `numeric_std` `is_x` | - | X | Same limitation as the `std_logic_1164` version. |
| `ieee.numeric_bit` | Y | D | Same operations on `bit`-based vectors. |
| `ieee.std_logic_arith` | Y | D | Legacy Synopsys; `conv_integer`, `conv_unsigned`, `conv_signed`, `conv_std_logic_vector`, `ext`, `shl`, `shr`. |
| `ieee.std_logic_unsigned` | Y | D | Arithmetic on `std_logic_vector` as unsigned. |
| `ieee.std_logic_signed` | Y | D | Arithmetic on `std_logic_vector` as signed. |
| `ieee.std_logic_misc` | Y | D | `and_reduce`, `or_reduce`, `xor_reduce`, and related helpers. |
| `ieee.fixed_pkg` | Y | X | Fixed-point types would need a new value model; not in the subset. |
| `ieee.float_pkg` | Y | X | Same as `fixed_pkg`. |
| `ieee.math_real` | P | S* | Static constant calculations only, including the documented constants and functions. No runtime real objects. |
| `ieee.textio` / `std.textio` | - | X | File I/O. |
| User package | Y | S | Declaration + body; types, subtypes, constants, subprograms, components. |
| Generic package instantiation (VHDL-2008) | Y | D | Section 4.2. |
| `use` of specific declarations or `.all` | Y | S | |
| Package-qualified names (`work.pkg.type`, `pkg.const`) | - | S | |

### 4.10 Attributes and synthesis attributes

| Attribute | Vivado | Tier | Notes |
|---|---|---|---|
| `'left`, `'right`, `'high`, `'low` | Y | S | Static folds where possible. |
| `'length` | Y | S | |
| `'ascending` | Y | S | |
| `'range`, `'reverse_range` | Y | S | Usable as a range constraint. |
| `'pos`, `'val` | Y | S | Enumeration position/value. |
| `'succ`, `'pred` | - | S | Enumeration successor/predecessor. |
| `'event` | Y | S* | Supported on the clock/sensitivity signal in recognized clock idioms; other uses are deferred. |
| `'stable` | - | D | Only in clock/sensitivity idioms if a lowering exists. |
| `'last_value`, `'last_event`, `'transaction`, `'quiet`, `'delayed` | - | X | Simulation-only event attributes; no hardware meaning. |
| `'image`, `'value` | - | X | String formatting/parsing; report/assert only. |
| `'base`, `'subtype` | - | D | Type marks in declarations; resolve during analysis. |
| `'element` | - | D | Array element subtype. |
| `'simple_name`, `'instance_name`, `'path_name` | - | X | String-valued; no model meaning. |
| Attribute declaration/specification for a custom attribute | Y | I | Parsed and ignored. |
| `ram_style`, `rom_style` | Y | I | No RAM/ROM mapping in Verilator. |
| `use_dsp`, `dsp_folding` | Y | I | |
| `keep`, `dont_touch` | Y | I | Could later map to Verilator keep/dead-code attributes; initially ignored. |
| `mark_debug` | Y | I | Could later map to tracing. |
| `async_reg`, `shreg_extract`, `max_fanout`, `io_buffer_type` | Y | I | |
| Attribute on an individual vector bit | Y (applies to all bits) | I | No effect. |
| Record containing attributes | N | X | UG901 restriction. |

### 4.11 VHDL-2008 features

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Unconstrained elements in arrays | Y | D | |
| Matching equality/inequality (`?=`, `?/=`) | Y | S | Don't-care positions in literals; no wildcard means same-value equality. |
| Condition operator (`??`) | Y | S | |
| Matching case (`case?`) | Y | D | |
| Simplified sensitivity lists (`process(all)`) | Y | S | |
| `elsif`/`else` in `if generate` | Y | S | |
| `case generate` | Y | D | |
| Enhanced bit-string literals | Y | D | Signed/unsigned and length-prefixed forms. |
| Block comments (`/* ... */`) | Y | S | |
| Protected types | Y | X | |
| Subprogram instantiation | Y | O | |
| Package instantiation | Y | D | |
| Block statement | Y | S | |
| External names | Y | X | Verilator hierarchy is not the VHDL hierarchy; accept only with a concrete lowering plan. |
| Array aggregates | Y | S | |
| Generic types | Y | S | Per-binding type specialization. |
| Generic subprograms | Y | O | |
| Reading output ports | Y | S | Output port values may be read inside the entity. |
| Expressions in port maps | Y | S | |
| Slices in aggregates | Y | S | |
| `boolean_vector`, `integer_vector` | Y | D | |
| Generics referencing generics | Y | S | |
| Generics in packages | Y | D | |
| Relaxed function return conversion | Y | D | |
| Extended globally/locally static expressions | Y | S* | Concatenation and similar static forms; the frontend folds what GHDL resolves and rejects the rest. |
| Static ranges from unconstrained objects | Y | S | Resolved at elaboration. |
| `context` declarations | Y | S | |
| Unconstrained array of unconstrained array | Y | D | |

### 4.12 VHDL-2019 features

VHDL-2019 is not the baseline. These rows are optional and must not delay the
VHDL-2008 subset. Vivado 2026.1 supports the listed subset.

| Feature | Vivado | Tier | Notes |
|---|---|---|---|
| Record interface with `view` (modport-like) | Y | O | Could be flattened to individual ports; no AST node planned. |
| Conditional identifiers / conditional analysis | Y | O | `VHDL_VERSION`, `TOOL_TYPE`, `TOOL_VENDOR`, `TOOL_NAME`, `TOOL_EDITION`, `TOOL_VERSION`. Values would be Verilator-specific, not Vivado's. |
| 64-bit `integer` | Y | O | Would change the baseline integer width. |
| Conditional expressions | Y | O | `expr when cond else expr`. |
| Conditional return | Y | O | |
| Optional trailing semicolon in interface lists | Y | O | Only in VHDL-2019 mode; GHDL under `--std=08` may reject it. |
| Partial connections in port maps | Y | O | `port(7 downto 4) => sig, port(3 downto 0) => open`. |
| Subtype inference from initial value | Y | O | |
| Subtype inference from function return context | Y | O | |
| Referencing earlier port elements in a port declaration | Y | O | |
| Regularized component declarations | Y | O | |
| Empty records | Y | O | |

### 4.13 Mixed-language designs

Vivado restricts mixing to design-unit instantiation. The frontend mirrors that
boundary and does not invent a shared netlist. The mixed-language milestone may
use a `--lib-create`-style wrapper/library flow rather than mixed sources in one
invocation.

| Rule | Vivado | Tier | Notes |
|---|---|---|---|
| VHDL entity instantiated from Verilog/SV | Y | D | Emit a wrapper/module with the entity name; normal SV instantiation binds by name. |
| Verilog module instantiated from VHDL | Y | D | Only through a VHDL `component` declaration with the same name, using default binding. No direct entity instantiation. |
| Component name lookup | Y | D | Search `work` for a VHDL unit first; otherwise treat the name as a Verilog module and search case-sensitively. Bind the first match. |
| Unified library namespace | Y | D | A VHDL entity and a Verilog module with the same name cannot coexist; report a duplicate. |
| Case sensitivity at the boundary | Y | D | Keep the original spelling of component and port names for the Verilog search. UG901 assumes Verilog ports are lowercase; Verilator honors the declared spelling. |
| Explicit port association | Y | D | Both directions. Formal and actual names must be given. |
| Parameter/generic override | Y | D | Named at instantiation, not positional or `defparam`. VHDL generics: `integer`, `real`, `string`, `boolean`. |
| No `defparam` | N | X | Verilog parameter override is named at instantiation only. |
| Only Verilog modules are visible to VHDL | Y | D | No Verilog packages, interfaces, or primitives cross the boundary. |
| Direct entity instantiation of a Verilog module from VHDL | N | X | Use a `component` declaration and default binding. |
| Component configuration for a Verilog module | N | X | Only default binding is supported. |
| VHDL `buffer`/`linkage` ports across the boundary | N | X | |
| Bidirectional pass elements in Verilog | N | X | |
| Unnamed Verilog ports | N | X | |
| Port types VHDL-to-Verilog | Y | D | `bit`, `bit_vector`, `std_logic`, `std_ulogic`, `std_logic_vector`, `std_ulogic_vector`; directions `in`, `out`, `inout`. |
| Port types Verilog-to-VHDL | Y | D | `wire`, `reg`; directions `input`, `output`, `inout`. |

## 5. Semantic decisions and documented divergences

### 5.1 Value model

Verilator's runtime is two-state; `V3Number` constants can encode 0, 1, X, Z.
VHDL's nine-valued `std_logic` is therefore collapsed at elaboration:

| VHDL | Verilator |
|---|---|
| `'0'`, `'L'` | 0 |
| `'1'`, `'H'` | 1 |
| `'X'`, `'U'`, `'W'`, `'-'` | X (constant); at runtime the model has 0/1 |
| `'Z'` | Z/high-impedance only where the tristate lowering supports it; otherwise a clean `Unsupported` |

Consequences:

- `-` is treated as an unknown data value, not as a synthesis don't-care.
- `U` and `W` are accepted and mapped to X; Vivado rejects them.
- A `std_logic` comparison with an X constant follows case-equality semantics;
  this is not full VHDL 9-state resolution.
- `'Z'` is not silently turned into 0. Designs that need buses must wait for the
  tristate/DFG core work, or use an explicit single-driver style.

### 5.2 Signals, variables, and scheduling

- VHDL signal assignment is a non-blocking update at the end of the current
  delta; it lowers to `AstAssignDly` inside a process and to `AstAssignW` for a
  concurrent assignment.
- VHDL variable assignment is immediate; it lowers to `AstAssign`.
- Verilator's NBA/settle scheduling approximates VHDL delta cycles. Ordering
  between independent processes is not guaranteed to match a VHDL simulator
  cycle-for-cycle, so multi-process designs need differential tests.
- A process without a sensitivity list and with a top-level clock wait is
  lowered to an edge-triggered `always` plus a time-zero initial.
- `process(all)` sensitivity is inferred from the signals read by the lowered
  body; process variables do not contribute.
- Explicit sensitivity lists are honored as written. Vivado synthesis adds
  missing signals and warns; Verilator does not, because that changes simulation
  semantics. Use `process(all)` for inference.

### 5.3 Multiple drivers and resolution

- One driver per signal is the baseline.
- `inout` ports and `std_logic` multi-driver resolution are limited by the
  current `V3Tristate`/DFG core behavior. The frontend reports a clean
  `Unsupported` when the model cannot represent the driver; it must not silently
  drop a driver.
- User-defined resolution functions are excluded until multi-driver support is
  designed. `std_logic_1164` resolution is the only resolution the subset
  eventually needs.

### 5.4 Integers, ranges, and checks

- `integer` is 32-bit signed. `natural` and `positive` are subtypes.
- Subtype ranges are checked statically during elaboration.
- Runtime range violations are not detected; this is a documented synthesis-
  oriented loss. A design that relies on VHDL runtime range errors is out of
  subset.
- Enumeration encoding follows declaration order, so `'pos`/`'val` and case
  choices agree with VHDL.
- VHDL-2019's 64-bit integer is not the baseline.

### 5.5 Timing constructs

- `after`, `transport`, `inertial`, `reject`, and `wait for` are excluded.
- Verilator rejects them rather than ignoring a delay, which would produce a
  model with different timing from the source. Vivado ignores delay because it
  is synthesizing; Verilator is also a simulator and must not silently change
  behavior.
- A future `--timing` mode could add a subset of these features. It is not part
  of the synthesis subset.

### 5.6 Assertions and reports

- Concurrent assertions are ignored by UG901 synthesis; static assertions can be
  evaluated with the `-assert` option.
- The frontend accepts `assert`/`report` syntax. Static assertions may be
  evaluated at elaboration. Runtime assertions are optional and, when enabled,
  lower to the existing assertion machinery. `report` has no effect.
- `severity` does not by itself reject a design; the policy is a frontend option,
  not a language rule.

### 5.7 Latches, registers, and inference

- The frontend does not infer hardware resources. A process with incomplete
  assignment describes a latch; Verilator models the storage and may warn.
- Clocked processes are recognized from `rising_edge`, `falling_edge`, and the
  supported `'event and level` idioms. Other event forms are rejected rather than
  guessed.
- Asynchronous reset/set must be in the sensitivity list and before the clock
  branch. Clock edges on signals outside the sensitivity list are rejected.

### 5.8 Names, hierarchy, and diagnostics

- VHDL identifiers keep their source spelling on emitted `AstVar`/pin/instance
  names; a folded form is used for scope lookup. This is required for the
  case-sensitive Verilog boundary.
- Generate labels and block labels become hierarchy components.
- Diagnostics use the existing `FileLine` mechanism and point at VHDL source
  locations. A construct that is outside the subset reports `Unsupported:` with
  the rule or feature name; it must not fall through to an assertion.

### 5.9 Libraries and analysis

- The frontend does not repeat VHDL semantic analysis when GHDL supplies the
  analyzed design units; GHDL is out-of-process, so the GPL boundary is a process
  boundary.
- Elaboration (architecture selection, component binding, generics, generate
  unrolling, unconstrained port specialization) happens in Verilator.
- A VHDL run never auto-loads SystemVerilog design units. Cross-language binding
  is an explicit later milestone.

### 5.10 Synthesis attributes

Attribute declarations and specifications are accepted and ignored. This keeps
vendor RTL readable without pretending Verilator maps to LUTs, DSPs, or BRAMs.
`keep`, `mark_debug`, `ram_style`, `rom_style`, `use_dsp`, `async_reg`, and
similar attributes have no effect on the model. A future mapping may translate
`keep` and `mark_debug` to Verilator's own attributes, but that is not required
for language conformance.

### 5.11 Characters, strings, and literals

- Logic character literals (`'0'`, `'1'`, `'X'`, `'U'`, `'W'`, `'-'`, `'L'`,
  `'H'`, `'Z'`) map through the value table above.
- Non-logic character literals and characters used as enum values are deferred
  where they require a full `character` value model. String generics and string
  literals used as arrays of characters are supported.
- Bit-string literals are accepted in the standard bases and at any width.
  Enhanced VHDL-2008 signed/unsigned forms are deferred.

### 5.12 VHDL-2019

VHDL-2019 is optional. When implemented, it must not change the behavior of
VHDL-2008 sources. Conditional analysis identifiers would report Verilator
values (`TOOL_TYPE` etc.), not Vivado's, so a design that branches on
`TOOL_VENDOR = "AMD"` would take the non-AMD branch. That is correct for a
Verilator run but differs from Vivado synthesis. Document this if the feature is
enabled.

### 5.13 Mixed language

The boundary contract is Section 4.13. The key decisions:

- Verilog module instantiated from VHDL: declare a VHDL component with the same
  name. Default binding searches `work` for a VHDL unit first, then a
  case-sensitive Verilog module.
- VHDL entity instantiated from Verilog: instantiate a module with the entity
  name. Binding searches Verilog modules first, then VHDL entities.
- Only design-unit instantiation crosses the boundary. No shared types, no
  package imports, no hierarchical references across languages.
- Generic/parameter override is named at instantiation. All parameters are
  passed, even when unchanged; `defparam` is rejected.
- The VHDL and Verilog libraries are logically unified for name lookup.
- Mixed source files in a single invocation are rejected until the wrapper/
  library flow is implemented.

### 5.14 Excluded constructs

The following are always rejected, with a specific reason:

| Construct | Reason |
|---|---|
| `access` types and `new` | No hardware meaning; pointer model. |
| Protected types | Verification/OS-like state; no hardware meaning. |
| File types, `textio`, `read`/`write`/`readline`/`writeline` | File I/O; no hardware meaning. |
| `force`, `release` | Verification-only. |
| `'image`, `'value`, string formatting | Report/assert only. |
| Physical objects at runtime, `now` | No hardware meaning. |
| Real objects (signals, ports, variables) | Synthesis excludes them; static calculations only. |
| `after`, `transport`, `inertial`, `reject`, `wait for` | Timing semantics; not in the synthesis subset. |
| User-defined resolution functions | Requires multi-driver resolution semantics. |
| External names | Verilator hierarchy is not the VHDL hierarchy. |
| Allocators, access deallocation | Same as access types. |
| Multi-dimensional ports | UG901 does not accept them as ports. |
| Zero-width/null arrays | No unambiguous hardware meaning. |
| `linkage` ports, `disconnect`, `guarded` signals | UG901 does not support them. |
| Deferred constants, `register`/`bus` signal kinds | UG901 does not support them. |
| Fixed/float packages | Would require a fixed-point/floating value model. |
| Embedded Verilog in VHDL or VHDL in Verilog | Mixing is instantiation-only. |

## 6. Acceptance and verification

The subset is only meaningful if it is tested. Required coverage:

| Layer | What | How |
|---|---|---|
| Language feature tests | One `t_vhdl_*` test per feature group or edge case | `test_regress/t/t_vhdl_*.py` |
| Negative tests | Every excluded construct and every deferred construct reports the documented diagnostic | `t_*_bad` tests with `.out` goldens |
| Elaboration tests | Generics, generic types/packages, generate, configurations, unconstrained ports, record/array specialization | VHDL corpus tests and AST dumps |
| Simulation equivalence | Random-vector comparison against GHDL where the semantics overlap | Differential harness, gated on `VERILATOR_GHDL` |
| Synthesis corpus | GHDL `synth` testsuite triage | `nodist/ghdl_synth_suite.py` |
| Design corpus | Counter, shift register, FIFO, FSM, UART TX/RX, AXI-Lite slave, RAM wrappers, DSP-style arithmetic | Regression designs |
| Mixed language | VHDL entity from SV and SV module from VHDL through the boundary rules | Separate mixed-language suite, later milestone |

Minimum corpus by feature area:

- Design units: entity/architecture/package/context/component/direct entity.
- Types: logic vectors, unsigned/signed, enums, records, 1-D arrays, arrays of
  arrays, unconstrained ports, integer subtypes.
- Expressions: arithmetic, logic, shifts/rotates, reductions, matching
  equality, aggregates, conversions, attributes.
- Sequential: if/case/for, clocked processes, async reset, wait-until,
  process(all), process splitting, variables.
- Concurrent: simple/conditional/selected assignments, generate forms, blocks.
- Packages: `std_logic_1164`, `numeric_std`, `numeric_bit`, legacy Synopsys
  packages, `math_real` static use.
- Rejections: access/protected/file, timing, real objects, multi-dimensional
  ports, user resolution, external names, deferred constants.
- Mixed language: both directions, case sensitivity, parameter override,
  missing component, duplicate unified name.

For each feature row marked `S`, there must be either an existing `t_vhdl_*`
test or a new one. For each row marked `X`, there must be a negative test that
proves the diagnostic and that no partial model is emitted. `D` rows become
`S` only together with their tests.

## 7. Open questions

These are feature-scope decisions that need a concrete answer before the
corresponding `D`/`O` item is scheduled:

1. Is `real` needed for anything beyond static generics and `math_real`? If not,
   keep the runtime exclusion permanent.
2. Should `numeric_bit` and the legacy Synopsys packages be implemented as
   first-class types, or lowered through `numeric_std` intrinsics?
3. Is multi-dimensional array support required internally, or can arrays of
   arrays cover the same designs? UG901 supports multi-dimensional arrays but
   rejects multi-dimensional ports.
4. Is `fixed_pkg`/`float_pkg` ever in scope? The current answer is no, because it
   requires a new value model.
5. Do VHDL-2019 interface views need to map to SystemVerilog interfaces, or can
   they flatten to individual ports?
6. Does the mixed-language flow need `--lib-create`-style wrappers first, or is
   direct in-process binding required?
7. Should runtime assertions be enabled by default, or only with an explicit
   option? UG901 only evaluates static assertions.
