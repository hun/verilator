-- DESCRIPTION: Verilator: VHDL a mixed-case --top-module name
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity CaseTop is
  port (y : out integer);
end;

architecture rtl of CaseTop is
begin
  y <= 1;
end;
