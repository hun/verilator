-- DESCRIPTION: Verilator: a function returning a declared array type without bounds
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The result takes its length from the returned value, as for an unconstrained std type

entity t is end entity;
architecture a of t is
  type int_array is array (natural range <>) of integer;
  function add(x : int_array; y : integer) return int_array is
    variable tmp : int_array(1 to 3);
  begin
    for i in 1 to 3 loop
      tmp(i) := x(i) + y;
    end loop;
    return tmp;
  end function;
begin
  process
    variable z : int_array(1 to 3) := (1, 2, 3);
    variable r : int_array(1 to 3);
  begin
    r := add(z, 10);
    assert r(1) = 11 and r(2) = 12 and r(3) = 13 report "add" severity failure;
    wait;
  end process;
end architecture;
