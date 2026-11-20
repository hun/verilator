-- DESCRIPTION: Verilator: VHDL subprogram of the wrong class is refused
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A function is not a statement and a procedure is not a value.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  function f(x : integer) return integer is
  begin
    return x;
  end function f;

  procedure p(x : integer) is
  begin
    null;
  end procedure p;
begin
  process
    variable a : integer := 1;
  begin
    f(a);
    y <= p;
    wait;
  end process;

  -- A concurrent statement is also a procedure call
  f(1);
end;
