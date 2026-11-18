-- DESCRIPTION: Verilator: VHDL constants respect their subtype range
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A constant's value is inside its declared subtype's bounds (IEEE 1076-2008
-- 5.4); GHDL reports the violation.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  subtype small is integer range 0 to 9;
  constant k : small := 20;
begin
  y <= k;
end;
