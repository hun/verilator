-- DESCRIPTION: Verilator: VHDL 'length needs an array prefix
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  y <= integer'length;
end;
