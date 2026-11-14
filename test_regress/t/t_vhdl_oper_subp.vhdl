-- DESCRIPTION: Verilator: VHDL subprogram whose name is an operator symbol
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package oper_pkg is
  -- `and` and `or` are defined for two boolean or two std_logic, never for a boolean
  -- and a std_logic, so these functions add to the operator set rather than hide one
  function "and" (left : boolean; right : std_logic) return boolean;
  function "or" (left : std_logic; right : boolean) return boolean;
end package;

library ieee;
use ieee.std_logic_1164.all;

package body oper_pkg is
  function "and" (left : boolean; right : std_logic) return boolean is
  begin
    -- Builtin `and` on two booleans, while an `and` subprogram is visible
    return left and (right = '1');
  end function "and";

  function "or" (left : std_logic; right : boolean) return boolean is
  begin
    return (left = '1') or right;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.oper_pkg.all;

entity t is
  port (
    i_a  : in  std_logic;
    i_b  : in  std_logic;
    o_ad : out std_logic;
    o_or : out std_logic
    );
end entity;

library ieee;
use ieee.std_logic_1164.all;
use work.oper_pkg.all;

architecture rtl of t is
begin
  -- The prefix form is how an operator symbol is called; the builtin operators of the
  -- surrounding expressions are untouched by these declarations
  o_ad <= '1' when "and"(i_a = '1', i_b) else '0';
  o_or <= '1' when "or"(i_b, i_a = '1') else '0';
end architecture;
