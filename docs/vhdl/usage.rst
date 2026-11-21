.. DESCRIPTION: Verilator: guide to using the native VHDL frontend
   SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
   SPDX-License-Identifier: CC0-1.0

.. _vhdl_usage:

===========
Using VHDL
===========

Verilator reads VHDL directly. A design file whose name ends in
:file:`.vhd` or :file:`.vhdl` is analyzed by the frontend built into
Verilator: there is no external analyzer to install, and the standard
packages (:file:`std_logic_1164`, :file:`numeric_std`, and their common
synthesis-era companions) are built in. Past analysis, a VHDL design is a
Verilator design like any other: the same command line, the same generated
C++, the same runtime library, and the same C++ testbench idiom as
:ref:`Example C++ Execution`.

The frontend accepts the synthesizable subset of IEEE 1076-2008 plus a
limited testbench idiom. What is inside that subset, and where the
two-state model diverges from VHDL, is documented in :doc:`limitations`; this
guide shows the flow rather than the full matrix.

----------------------
Selecting the language
----------------------

The file extension selects the language: :file:`.vhd` and :file:`.vhdl` are
VHDL; :file:`.v`, :file:`.sv`, :file:`.vh`, :file:`.svh`, and any extension
mapped by an option such as :vlopt:`+1800-2017ext+<ext>` are SystemVerilog. A
VHDL design kept in files with other extensions is selected with
:vlopt:`--language vhdl \<--language\>`:

.. code-block:: bash

   verilator --lint-only --language vhdl design.vhdl

The extension wins, so :vlopt:`--language vhdl \<--language\>` never
reinterprets a SystemVerilog source or header as VHDL.

----------------
A first design
----------------

We'll verilize a small counter and drive it from C++. Create a working
directory with the design, :file:`counter.vhdl`:

.. code-block:: vhdl

   library ieee;
   use ieee.std_logic_1164.all;

   entity counter is
     port (
       clk   : in  std_logic;
       rst   : in  std_logic;
       count : out integer
     );
   end entity;

   architecture rtl of counter is
     signal c : integer := 0;
   begin
     process (clk)
     begin
       if rising_edge(clk) then
         if rst = '1' then
           c <= 0;
         else
           c <= c + 1;
         end if;
       end if;
     end process;

     count <= c;
   end architecture;

and the testbench, :file:`sim_main.cpp`:

.. code-block:: C++

   #include "Vcounter.h"
   #include "verilated.h"
   #include <cstdio>

   int main(int argc, char** argv) {
       VerilatedContext* contextp = new VerilatedContext;
       contextp->commandArgs(argc, argv);
       Vcounter* top = new Vcounter{contextp};

       top->rst = 1;
       top->clk = 0;
       top->eval();

       // Ten clock edges; reset released after the second
       for (int i = 0; i < 10; i++) {
           top->rst = (i < 2);
           top->clk = !top->clk;
           top->eval();
       }
       std::printf("count = %d\n", top->count);

       delete top;
       delete contextp;
       return 0;
   }

Now run Verilator on the design and execute the model:

.. code-block:: bash

   verilator --cc --exe --build -j 0 counter.vhdl sim_main.cpp
   ./obj_dir/Vcounter

which prints:

.. code-block:: text

   count = 4

Breaking the command down:

#. :vlopt:`--cc` for C++ output.

#. :vlopt:`--exe`, along with the :file:`sim_main.cpp` wrapper file, so
   the build creates an executable instead of only a library.

#. :vlopt:`--build` so Verilator calls the toolchain itself, with
   :vlopt:`-j \<-j\>` 0 to parallelize it.

#. :file:`counter.vhdl`. The :file:`.vhdl` extension makes Verilator
   analyze the file as VHDL; no other option is needed.

What the C++ side sees:

- The model class is named after the design: :vlopt:`--top-module` names
  the top entity when that must be made explicit, and the class prefix
  otherwise follows :vlopt:`--prefix`, which defaults to V prepended to
  the name of the first design file. Name the file after the top entity
  and the class name follows.

- Ports become class members: a ``std_logic`` port is one byte, an
  ``integer`` port 32 bits, and an ``unsigned(W-1 downto 0)`` port the
  smallest of 8/16/32/64 bits that holds it. Evaluate with ``eval()``
  after every input change, exactly as for a SystemVerilog design.

- The model is two-state: a signal that VHDL would leave at ``'U'`` starts
  at 0 in the model. See the known divergences in :doc:`limitations` for
  what this means for metavalue checks and resolution.

-----------------------
A hierarchical design
-----------------------

The second design is an accumulator that instantiates an adder. One file,
:file:`accum.vhdl`, holds both entities, and the accumulator passes its
generic down with a generic map:

.. code-block:: vhdl

   library ieee;
   use ieee.std_logic_1164.all;
   use ieee.numeric_std.all;

   entity adder is
     generic (WIDTH : positive := 8);
     port (
       a    : in  unsigned(WIDTH-1 downto 0);
       b    : in  unsigned(WIDTH-1 downto 0);
       cin  : in  std_logic;
       sum  : out unsigned(WIDTH-1 downto 0);
       cout : out std_logic
     );
   end entity;

   architecture rtl of adder is
     signal full : unsigned(WIDTH downto 0);
   begin
     full <= ('0' & a) + ('0' & b) + ("" & cin);
     sum  <= full(WIDTH-1 downto 0);
     cout <= full(WIDTH);
   end architecture;

   entity accum is
     generic (WIDTH : positive := 8);
     port (
       clk   : in  std_logic;
       rst   : in  std_logic;
       en    : in  std_logic;
       d     : in  unsigned(WIDTH-1 downto 0);
       q     : out unsigned(WIDTH-1 downto 0);
       carry : out std_logic
     );
   end entity;

   architecture rtl of accum is
     signal acc      : unsigned(WIDTH-1 downto 0) := (others => '0');
     signal next_sum : unsigned(WIDTH-1 downto 0);
   begin
     u_add : entity work.adder
       generic map (WIDTH => WIDTH)
       port map (a => acc, b => d, cin => '0',
                 sum => next_sum, cout => carry);

     process (clk)
     begin
       if rising_edge(clk) then
         if rst = '1' then
           acc <= (others => '0');
         elsif en = '1' then
           acc <= next_sum;
         end if;
       end if;
     end process;

     q <= acc;
   end architecture;

and the testbench, :file:`sim_main.cpp`, drives it like the counter:

.. code-block:: C++

   #include "Vaccum.h"
   #include "verilated.h"
   #include <cstdio>

   int main(int argc, char** argv) {
       VerilatedContext* contextp = new VerilatedContext;
       contextp->commandArgs(argc, argv);
       Vaccum* top = new Vaccum{contextp};

       top->rst = 1;
       top->en = 0;
       top->d = 3;
       top->clk = 0;
       top->eval();

       // Eight clock edges; reset released after the second
       for (int i = 0; i < 8; i++) {
           top->rst = (i < 2);
           top->en = (i >= 2);
           top->clk = !top->clk;
           top->eval();
           std::printf("cycle %d: q = %u\n", i, top->q);
       }

       delete top;
       delete contextp;
       return 0;
   }

Run Verilator on the pair and execute the model:

.. code-block:: bash

   verilator --cc --exe --build -j 0 --top-module accum accum.vhdl sim_main.cpp
   ./obj_dir/Vaccum

which prints:

.. code-block:: text

   cycle 0: q = 0
   cycle 1: q = 0
   cycle 2: q = 3
   cycle 3: q = 3
   cycle 4: q = 6
   cycle 5: q = 6
   cycle 6: q = 9
   cycle 7: q = 9

What this example adds over the counter:

- The file holds two entities, so :vlopt:`--top-module accum` names the one
  to make top.

- The instance binds by direct entity instantiation, ``entity
  work.adder``. A component declaration followed by component
  instantiation binds the same way.

- The generic ``WIDTH`` specializes per binding, like a SystemVerilog
  parameter: each ``generic map`` gets its own generated code. A top-level
  generic takes its declared default; :vlopt:`-G \<-G\<name\>\>` overrides
  SystemVerilog parameters, not VHDL generics, so give the entity the
  default the design needs, or wrap it under a top that binds the generic
  with a ``generic map``.

- In the testbench, ``d`` and ``q`` are byte-wide members, since ``WIDTH``
  defaults to 8.

--------------------------------------
Mixed VHDL and SystemVerilog sources
--------------------------------------

One invocation may analyze both languages: each file is analyzed by the
frontend of its own language, chosen by the file extension or by
:vlopt:`--language vhdl \<--language\>`, and both feed one elaborated
netlist. A SystemVerilog design therefore instantiates a VHDL entity by
name:

.. code-block:: sv

   module top (input logic clk, input logic en, output logic [3:0] q);
     mixed_dut dut (.clk(clk), .en(en), .q(q));
   endmodule

.. code-block:: bash

   verilator --cc --exe --build -j 0 top.sv sim_main.cpp dut.vhdl
   obj_dir/Vtop

where :file:`dut.vhdl` declares ``entity mixed_dut``. The instance binds
like any other module instance: the port names are the entity's, folded to
lower case, with the widths the entity declares. Each language elaborates
its own hierarchy, so a VHDL entity may instantiate further VHDL entities
below the SystemVerilog top. A SystemVerilog testbench that generates its own
clock needs :vlopt:`--timing`; the VHDL side contributes no delays of its own.
Name every design file on the command line:
an invocation that holds VHDL sources does not search :vlopt:`-y`/`-v`
directories for SystemVerilog modules.

The alternative is the library boundary, which is what a VHDL design
compiled on its own or used from another simulator needs. One run builds
the VHDL hierarchy into a DPI library together with a SystemVerilog
wrapper module, and a second run instantiates that wrapper and links the
library:

.. code-block:: bash

   # Run 1: the VHDL design into a DPI library and the wrapper module vhdlsub
   verilator --language vhdl --cc -Mdir obj_vhdl --prefix Vvhdlsub \
       --lib-create vhdlsub --top-module dut --timescale-override /1fs \
       dut.vhdl
   make -C obj_vhdl -f Vvhdlsub.mk

   # Run 2: the SystemVerilog top instantiates the wrapper, links the library
   verilator --binary -LDFLAGS ../obj_vhdl/libvhdlsub.a \
       top.sv obj_vhdl/vhdlsub.sv
   obj_dir/Vtop

:file:`top.sv` instantiates the wrapper like any other module of the
design:

.. code-block:: sv

   module top;
     logic clk;
     logic [3:0] q;
     vhdlsub dut (.clk(clk), .q(q));
     // Drive clk; read q
   endmodule

What the two runs exchange:

- ``vhdlsub``, the argument of :vlopt:`--lib-create`, is both the library
  name (:file:`libvhdlsub.a` and :file:`libvhdlsub.so`) and the name of
  the generated wrapper module (:file:`vhdlsub.sv`), which is what
  :file:`top.sv` instantiates. ``--prefix Vvhdlsub`` names the generated
  model class and the Makefile.

- The wrapper's ports are the VHDL entity's ports, with the names folded
  to lower case and the widths the entity declares, so the instantiation
  in :file:`top.sv` names them as the VHDL source does.

- :vlopt:`--top-module` selects the entity that becomes the library top
  when a design file declares more than one, the same way it selects
  among SystemVerilog modules.

- :vlopt:`-LDFLAGS` takes a path relative to the directory the second run
  builds in (:file:`obj_dir` by default), so a library built in a sibling
  directory is referenced as :file:`../obj_vhdl/libvhdlsub.a`.

- The library run is a complete Verilator run: it elaborates the VHDL
  design, so the wrapper is a compiled model, and any simulator that can
  call a DPI library can also use it. :vlopt:`--lib-create` requires a
  single top level module and rules out :vlopt:`--timing` delays and
  :vlopt:`--sc`.

One direction stays unsupported: a VHDL design instantiating a
SystemVerilog module. A VHDL instance binds only to entities analyzed with
it, so

.. code-block:: vhdl

   u1 : svmod port map (a => i, y => o);

fails with ``Cannot find VHDL entity 'svmod' for instance 'u1'``, even
when :file:`svmod.sv` is on the command line. Where the hierarchy allows
it, restructure: instantiate the VHDL design from a SystemVerilog top, as
above, or port the leaf module to VHDL. There is no wrapper that makes a
VHDL top see a SystemVerilog module.

Parts that are each self-contained in one language can also be analyzed
separately, for example to lint each in its own invocation:

.. code-block:: bash

   verilator --lint-only top.vhdl
   verilator --lint-only helper.sv

----------------
Linting a design
----------------

:vlopt:`--lint-only` analyzes and elaborates the design and stops without
generating C++:

.. code-block:: bash

   verilator --lint-only accum.vhdl

The checks the frontend performs include the ones a VHDL analyzer makes:
a case statement must cover every value of its selection type and name no
choice twice, an integer literal or constant must respect its declared
range, two processes may not drive the same unresolved bit, and a port
actual must conform to the formal it binds. Unsupported constructs are
refused with a source-located ``Unsupported:`` diagnostic rather than
accepted silently. The complete support matrix is in :doc:`limitations`.

----------------
Where to go next
----------------

- :doc:`architecture` for how the frontend is built,
- :doc:`limitations` for the supported subset and known divergences,
- :doc:`../guide/connecting` and :doc:`../guide/simulating` for everything
  between the generated model and a running simulation, which is the same
  for VHDL and SystemVerilog designs.
