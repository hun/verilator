-- DESCRIPTION: Verilator: a character literal in a concatenation is one logic element
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A character literal concatenated to a logic vector is a single logic value,
-- even when the vector is eight bits wide: `'0' & v` is nine bits with a zero
-- in the top bit, not the eight-bit character code of '0' (IEEE 1076-2008 9.2.2).

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    v  : in  std_logic_vector(7 downto 0);
    x  : in  std_logic_vector(8 downto 0);
    lo : out std_logic_vector(8 downto 0);
    hi : out std_logic_vector(8 downto 0)
  );
end entity;

architecture rtl of t is
begin
  -- A character code would make this sixteen bits wide and not match x
  lo <= ('0' & v) xor x;
  hi <= v & '1';
end architecture;
