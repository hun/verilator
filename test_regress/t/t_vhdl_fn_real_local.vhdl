-- DESCRIPTION: Verilator: a real local updated from its previous value in a function
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The local is bound to a constant after each assignment, and a later read still sees its
-- fraction

entity t is end entity;

architecture a of t is
  type arr_t is array (0 to 3) of integer;
  function walk return arr_t is
    variable r : arr_t := (others => 0);
    constant step : real := 0.5;
    variable x : real := 1.0;
  begin
    for i in r'range loop
      r(i) := integer(x * 10.0);
      x := x + step;
    end loop;
    return r;
  end function;
  function held return integer is
    variable x : real := 1.0;
  begin
    x := 1.25;
    return integer(x * 1000.0);
  end function;
  constant w : arr_t := walk;
  constant h : integer := held;
begin
  process begin
    wait for 1 ns;
    assert w(0) = 10 and w(1) = 15 and w(2) = 20 and w(3) = 25 report "walk" severity failure;
    assert h = 1250 report "held" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
