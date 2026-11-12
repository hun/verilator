-- DESCRIPTION: Verilator: VHDL discrete ranges taken from a type mark
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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
