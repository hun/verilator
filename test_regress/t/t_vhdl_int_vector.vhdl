-- DESCRIPTION: Verilator: the predefined integer_vector, and arithmetic over its elements
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- poc's memory package plans its RAM use with an `integer_vector` a function builds, and
-- its OCRAM entity reads the plan back element by element to size three memory ranges.
-- The unit also declares `*` and `+` over `integer_vector`, so the integer arithmetic on
-- the plan's elements has to resolve against those overloads.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package ramplan is
  function ite(c : boolean; a : integer; b : integer) return integer;
  function plan(depth : positive; width : positive) return integer_vector;
  function "*"(vec : integer_vector; i : integer) return integer_vector;
  function "+"(vec : integer_vector; i : integer) return integer_vector;
end package;

package body ramplan is
  function ite(c : boolean; a : integer; b : integer) return integer is
  begin
    if c then
      return a;
    end if;
    return b;
  end function;

  function plan(depth : positive; width : positive) return integer_vector is
    variable r : integer_vector(0 to 1) := (others => 0);
  begin
    if width <= 16 then
      r(0) := depth / 4;
      r(1) := width / 4;
    else
      r(0) := 0;
      r(1) := width / 8;
    end if;
    return r;
  end function;

  -- Never called by the design: their presence is what makes an untyped operand ambiguous
  function "*"(vec : integer_vector; i : integer) return integer_vector is
  begin
    return vec;
  end function;

  function "+"(vec : integer_vector; i : integer) return integer_vector is
  begin
    return vec;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.ramplan.all;

entity t is
  generic (
    DEPTH : positive := 512;
    WIDTH : positive := 32
  );
  port (
    clk   : in  std_logic;
    o_uram : out std_logic_vector(7 downto 0);
    o_bram : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
  -- An unconstrained array constant takes its length from the function result
  constant plan_c : integer_vector := plan(DEPTH, WIDTH);

  constant U_High_Bit : integer :=
    ite((plan_c(0) * 72) > WIDTH, WIDTH, (plan_c(0) * 72)) - 1;
  constant B_Low_Bit  : integer := U_High_Bit + 1;
  constant B_High_Bit : integer :=
    ite((plan_c(1) * 4 + U_High_Bit + 1) > WIDTH, WIDTH - 1,
        (plan_c(1) * 4 + U_High_Bit));
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o_uram <= std_logic_vector(to_unsigned(plan_c(0), 8));
      o_bram <= std_logic_vector(to_unsigned(B_High_Bit - B_Low_Bit + 1, 8));
    end if;
  end process;
end architecture;
