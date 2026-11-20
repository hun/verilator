-- DESCRIPTION: Verilator: VHDL static integer expressions do not overflow
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- IEEE 1076-2008 9.1: the integer type is 32 bits.

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  y <= integer'high + 1;
  y <= 2 ** 31;
  y <= 2 ** 40;
end;
