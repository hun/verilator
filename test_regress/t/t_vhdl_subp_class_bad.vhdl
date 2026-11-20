-- DESCRIPTION: Verilator: VHDL subprogram of the wrong class is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
