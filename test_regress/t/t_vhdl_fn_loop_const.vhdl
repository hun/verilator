-- DESCRIPTION: Verilator: a constant initialized by a function loop that only a run-time value ends
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Nothing folds, so there is no count to unroll to: the loop runs at run time

-- A constant initialized from a signal has no value to fold, so it follows the signal

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is end entity;

architecture rtl of t is
  function width_of(v : integer) return integer is
    variable i : integer := 0;
  begin
    while 2 ** i < v loop
      i := i + 1;
    end loop;
    return i;
  end function;
  signal x : unsigned(7 downto 0) := to_unsigned(5, 8);
  constant c : integer := width_of(to_integer(x));
  constant d, e : integer := c + 1;
begin
  process
  begin
    wait for 1 ns;
    assert c = 3 report "x = 5" severity failure;
    x <= to_unsigned(200, 8);
    wait for 1 ns;
    assert c = 8 report "x = 200" severity failure;
    assert d = 9 and e = 9 report "d, e" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
