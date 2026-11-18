-- DESCRIPTION: Verilator: VHDL abs on a numeric_std signed vector
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    s  : in  signed(7 downto 0);
    a1 : out signed(7 downto 0)
  );
end;

architecture rtl of t is
begin
  -- numeric_std defines "abs" for a signed operand (IEEE 1076-2008 Annex C).
  -- GHDL: abs(-2) = 2.
  a1 <= abs(s);
end;
