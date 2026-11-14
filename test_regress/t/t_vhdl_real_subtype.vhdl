-- DESCRIPTION: Verilator: VHDL subtype of real that carries a range constraint
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package real_pkg is
  subtype binary_real_t is real range 0.0 to 1.0;
  subtype binary_integer_t is integer range 0 to 1;
  function to_int(value : boolean) return binary_integer_t;
end package;

library ieee;
use ieee.std_logic_1164.all;

package body real_pkg is
  function to_int(value : boolean) return binary_integer_t is
  begin
    if value then
      return 1;
    end if;
    return 0;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.real_pkg.all;

entity t is
  port (
    i_a     : in  std_logic;
    o_bit   : out integer range 0 to 1;
    o_above : out integer range 0 to 1
    );
end entity;

library ieee;
use ieee.std_logic_1164.all;
use work.real_pkg.all;

architecture rtl of t is
  constant half : binary_real_t := 0.5;
  -- A comparison of the constrained real subtype, folded at elaboration
  constant above_quarter : integer range 0 to 1 := to_int(half > 0.25);
begin
  o_bit <= to_int(i_a = '1');
  o_above <= above_quarter;
end architecture;
