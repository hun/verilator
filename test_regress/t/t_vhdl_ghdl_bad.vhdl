-- DESCRIPTION: Verilator: malformed VHDL source, GHDL XML frontend
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (
    a : in bit
  );
end;

architecture rtl of t is
begin
  y <= a
end;
