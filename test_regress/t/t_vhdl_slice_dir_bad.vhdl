-- DESCRIPTION: Verilator: a slice whose range opposes the array direction is refused
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    i : in  unsigned(7 downto 0);
    o : out unsigned(7 downto 0)
  );
end entity t;

architecture rtl of t is
  -- Ascending over a descending index: GHDL stops elaboration with
  -- `slice direction doesn't match index direction`
  subtype asc_range_t is natural range 4 to 7;
  signal v : unsigned(7 downto 0);
begin
  v <= i;
  o <= v(asc_range_t);
end architecture rtl;
