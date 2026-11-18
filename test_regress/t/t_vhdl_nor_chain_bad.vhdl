-- DESCRIPTION: Verilator: VHDL non-associative logical operators need parentheses
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- The operators nand and nor are non-associative, and operators of one
-- precedence level may not be mixed without parentheses (IEEE 1076-2008 9.1).
-- GHDL refuses every expression below.

entity t is
  port (
    a : in  bit;
    b : in  bit;
    c : in  bit;
    y : out bit
  );
end;

architecture rtl of t is
begin
  y <= a nor b nor c;
end;
