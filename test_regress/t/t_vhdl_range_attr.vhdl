-- DESCRIPTION: Verilator: VHDL discrete ranges taken from a type mark
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in integer;
    y : out integer
  );
end;

architecture rtl of t is
  type mem_t is array (0 to 7) of std_logic;
  type dwn_t is array (9 downto 0) of std_logic;
  -- Discrete range attributes of a type mark, in ascending, descending and
  -- reversed forms (IEEE 1800-2023 5.2.3.3); no 'to'/'downto' follows here
  signal idx : integer range mem_t'range;
  subtype d_idx is integer range dwn_t'range;
  signal d : d_idx;
  constant c : integer range mem_t'reverse_range := 3;
begin
  process(a)
  begin
    idx <= a;
    d <= a;
  end process;
  y <= idx + d + (c - 3);
end;
