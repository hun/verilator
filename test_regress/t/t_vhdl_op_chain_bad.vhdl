-- DESCRIPTION: Verilator: VHDL relational operators cannot be chained
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic;
    b : in  std_logic;
    c : in  std_logic;
    y : out std_logic
  );
end;

architecture rtl of t is
begin
  -- IEEE 1076-2008 9.1: relational operators are non-associative
  y <= '1' when a = b = c else '0';
end;
