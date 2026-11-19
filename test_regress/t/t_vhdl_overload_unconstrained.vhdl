-- DESCRIPTION: Verilator: an overload takes a constrained actual for an unconstrained formal
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- poc's utility package has an `ite` per type, the string one taking unconstrained strings,
-- and its configuration passes a constrained string subtype to it. Two values of the same
-- base type match whatever their constraints (IEEE 1076-2008 12.5), so the string overload
-- is the one that takes the constrained actuals.

library ieee;
use ieee.std_logic_1164.all;

package pkg is
  subtype cfg_s is string(1 to 4);

  function pick(c : boolean; a : string; b : string) return string;
  function pick(c : boolean; a : integer; b : integer) return integer;
end package;

package body pkg is
  function pick(c : boolean; a : string; b : string) return string is
  begin
    if c then return a; else return b; end if;
  end function;

  function pick(c : boolean; a : integer; b : integer) return integer is
  begin
    if c then return a; else return b; end if;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;

use work.pkg.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out std_logic_vector(31 downto 0);
    n   : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t is
  constant first_c : cfg_s := "abcd";
  constant other_c : cfg_s := "wxyz";
  -- The actuals are constrained, the string formal is not
  constant taken_c : cfg_s := pick(true, first_c, other_c);
  constant count_c : integer := pick(true, 3, 9);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o <= taken_c(1) & taken_c(2) & taken_c(3) & taken_c(4);
      n <= std_logic_vector(to_unsigned(count_c, 4));
    end if;
  end process;
end architecture;
