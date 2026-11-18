-- DESCRIPTION: Verilator: VHDL integer literal range
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- An integer literal is inside the integer type's range (IEEE 1076-2008 15.5);
-- GHDL refuses this one, which the 32-bit model would wrap.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  constant big : integer := 4000000000;
begin
  y <= big;
end;
