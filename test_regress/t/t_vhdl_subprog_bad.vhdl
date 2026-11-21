-- DESCRIPTION: Verilator: VHDL subprogram diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Assignments to formals, reads of out formals, unknown and ambiguous subprograms.

entity top is
  port (x : out integer);
end entity;

architecture rtl of top is
  procedure p (a : in integer) is
  begin
    a := 2;
  end procedure;

  procedure q (signal s : out bit; signal t : out bit) is
  begin
    t <= s;
  end procedure;

  function f return integer is
  begin
    return 1;
  end function;

  function f return bit is
  begin
    return '1';
  end function;

  signal sb : bit;
  signal tb : bit;
begin
  process
  begin
    p(1);
    q(sb, tb);
    nosuch(1);
    x <= f;
    wait;
  end process;
end architecture;
