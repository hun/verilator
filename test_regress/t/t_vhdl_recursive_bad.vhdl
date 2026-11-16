-- DESCRIPTION: Verilator: a recursive VHDL function has no cycle model
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_recursive is end entity;
architecture rtl of t_recursive is
  function f(n : natural) return natural is
  begin
    if n = 0 then
      return 0;
    end if;
    return f(n - 1);
  end function;
  constant c : natural := f(3);
begin
end architecture;
