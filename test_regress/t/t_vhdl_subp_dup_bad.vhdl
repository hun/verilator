-- DESCRIPTION: Verilator: VHDL subprogram bodies are unique
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (y : out integer);
end;

architecture rtl of t is
  function f(x : integer) return integer is
  begin
    return x;
  end function f;
  function f(x : integer) return integer is
  begin
    return x + 1;
  end function f;
begin
  y <= f(1);
end;
