-- DESCRIPTION: Verilator: VHDL std_logic identity equality
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic_vector(3 downto 0));
end;

architecture rtl of t is
  constant x  : std_logic := 'X';
  constant xv : std_logic_vector(3 downto 0) := "XXXX";
begin
  -- IEEE 1164 equality is identity, so an unknown compares equal to itself
  o(0) <= '1' when x = 'X' else '0';
  o(1) <= '1' when x /= '0' else '0';
  o(2) <= '1' when xv = "XXXX" else '0';
  o(3) <= '1' when xv /= "0000" else '0';
end;
