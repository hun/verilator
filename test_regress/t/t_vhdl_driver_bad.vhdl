-- DESCRIPTION: Verilator: VHDL unresolved signals have one driver
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- An unresolved signal may have only one driver (IEEE 1076-2008 12.6.4); GHDL
-- refuses this design at elaboration.

entity t is
  port (
    a : in  bit;
    b : in  bit;
    y : out bit
  );
end;

architecture rtl of t is
  signal s : bit;  -- `bit` has no resolution function
begin
  s <= a;
  s <= b;
  y <= s;
end;
