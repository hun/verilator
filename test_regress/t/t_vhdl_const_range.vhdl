-- DESCRIPTION: Verilator: VHDL array constrained by a subtype mark and a
-- constant's 'range used as a slice bound
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    q     : out std_logic_vector(3 downto 0);
    lo_o  : out integer;
    hi_o  : out integer;
    len_o : out integer
  );
end entity t;

architecture rtl of t is
  subtype nibble_t is natural range 3 downto 0;
  -- The index constraint names an integer subtype, not a range expression
  constant c : u_unsigned(nibble_t) := "1010";
  signal s : std_logic_vector(7 downto 0) := x"A5";
begin
  q     <= s(c'range);
  lo_o  <= c'low;
  hi_o  <= c'high;
  len_o <= c'length;
end architecture rtl;
