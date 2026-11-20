-- DESCRIPTION: Verilator: VHDL overloads across subprogram classes resolve by context
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A function and a procedure with the same name and profile are distinct
-- overloads; the statement context selects the procedure and the value context
-- selects the function.

entity tb is
end;

architecture sim of tb is
  function op(x : integer) return integer is
  begin
    return x + 1;
  end function op;

  procedure op(x : integer) is
  begin
    null;
  end procedure op;

  signal y : integer := 0;
begin
  process
  begin
    op(3);
    y <= op(3);
    wait for 1 ns;
    assert y = 4 report "wrong overload resolution" severity failure;
    std.env.finish;
    wait;
  end process;
end;
