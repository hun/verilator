-- DESCRIPTION: Verilator: VHDL plain variables are not entity declarations
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (y : out integer);
  variable v : integer;
end;

architecture rtl of t is
begin
  y <= 1;
end;
