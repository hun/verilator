-- DESCRIPTION: Verilator: VHDL disconnection specifications are refused
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- The subset does not list disconnection specifications, so a silent skip
-- would drop a rule the hardware model needs.

entity t is
  port (y : out integer);
end;

architecture rtl of t is
  disconnect all : bit after 1 ns;
begin
  y <= 1;
end;
