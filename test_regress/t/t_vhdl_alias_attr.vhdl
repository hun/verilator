-- DESCRIPTION: Verilator: an alias whose mark reads an attribute of a signal of the same part
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
  signal cnt : unsigned(7 downto 0);
  -- The mark reads cnt'length, so the alias can only be registered once cnt exists
  alias dna : unsigned(cnt'length - 1 downto 0) is cnt;
begin
  cnt <= i;
  o   <= dna;
end architecture rtl;
