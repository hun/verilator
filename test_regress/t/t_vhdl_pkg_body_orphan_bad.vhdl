-- DESCRIPTION: Verilator: a VHDL package body needs a specification
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package body p is
  constant c : integer := 1;
end package body;

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  y <= 1;
end;
