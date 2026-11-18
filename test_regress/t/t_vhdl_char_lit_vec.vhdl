-- DESCRIPTION: Verilator: VHDL a scalar character literal in a vector context
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    v  : in  std_logic_vector(7 downto 0);
    y1 : out std_logic_vector(7 downto 0);
    y2 : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
begin
  -- The scalar/array logical operators of std_logic_1164 combine element-wise
  -- with the logic value of the literal (IEEE 1076-2008 9.2.2), not with its
  -- character code: '1' is one, never 16#31#. GHDL reports y1 = y2 = v.
  y1 <= v and '1';
  y2 <= '0' or v;
end;
