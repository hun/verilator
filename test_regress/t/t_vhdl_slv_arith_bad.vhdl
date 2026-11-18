-- DESCRIPTION: Verilator: VHDL std_logic_vector arithmetic needs a package
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- The predefined arithmetic operators have bit and boolean elements (IEEE
-- 1076-2008 9.1); a std_logic_vector needs std_logic_unsigned or
-- std_logic_arith visible, so GHDL refuses this design.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic_vector(3 downto 0);
    b : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  y <= a + b;
end;
