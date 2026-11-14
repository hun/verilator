-- DESCRIPTION: Verilator: VHDL variable declared with an object's 'subtype attribute
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- Both variables take the subtype of an object instead of naming a type. `shifted` takes
-- that of a signal, whose bounds are static and so also show in its 'length attribute.
-- `copy` takes that of a subprogram's own formal, which is the shape poc's utils.vhdl
-- vector operators use.

library ieee;
use ieee.std_logic_1164.all;

package p is
  type vec_t is array (natural range <>) of integer;
  function element(vec : vec_t; index : integer) return integer;
end package p;

package body p is
  function element(vec : vec_t; index : integer) return integer is
    variable copy : vec'subtype;
  begin
    copy(index) := vec(index);
    return copy(index);
  end function element;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use work.p.all;

entity t is
  port (
    i_a   : in  integer;
    o_sum : out integer;
    o_len : out integer
  );
end entity t;

architecture rtl of t is
  signal data : vec_t(0 to 3) := (10, 20, 30, 40);
begin
  process (data, i_a)
    variable shifted : data'subtype;
  begin
    for i in data'range loop
      shifted(i) := data(i) + i_a;
    end loop;
    o_sum <= shifted(0) + shifted(3) + element(data, 1);
    o_len <= shifted'length;
  end process;
end architecture rtl;
