-- DESCRIPTION: Verilator: a function loop that only a run-time value ends
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Nothing folds, so there is no count to unroll to: the loop runs at run time

library ieee;
use ieee.std_logic_1164.all;

entity t is end entity;

architecture rtl of t is
  function first(v : std_logic_vector) return integer is
    variable i : natural := 0;
  begin
    loop
      exit when v(i mod 4) = '1';
      i := i + 1;
    end loop;
    return i;
  end function;
  signal x : std_logic_vector(3 downto 0) := "0100";
  signal o : integer range 0 to 7;
begin
  o <= first(x);
  process
  begin
    wait for 1 ns;
    assert o = 2 report "x = 0100" severity failure;
    x <= "1010";
    wait for 1 ns;
    assert o = 1 report "x = 1010" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
