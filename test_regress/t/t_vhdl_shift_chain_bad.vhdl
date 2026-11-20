-- DESCRIPTION: Verilator: VHDL shift operators cannot be chained
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- IEEE 1076-2008 9.1: shift operators are non-associative
  y <= a sll 1 sll 2;
end;
