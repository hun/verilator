-- DESCRIPTION: Verilator: an array local written under a run-time condition
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The element write only happens when the run-time condition holds, so the function
-- runs as statements instead of one expression

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is end entity;

architecture rtl of t is
  type arr_t is array (natural range <>) of unsigned(7 downto 0);
  function f(v : unsigned; c : std_logic) return unsigned is
    variable a : arr_t(0 to 1) := (others => (others => '0'));
  begin
    a(0) := resize(v, 8);
    if c = '1' then
      a(1)(3) := '1';
    end if;
    return a(0) or a(1);
  end function;
  -- Returns from inside nested loops, with the loop bound read from a signal
  function first_set(v : unsigned(7 downto 0); n : integer) return integer is
  begin
    for i in 0 to 7 loop
      if i >= n then
        return 100;
      end if;
      for j in 0 to 1 loop
        if v(i) = '1' and j = 1 then
          return i;
        end if;
      end loop;
    end loop;
    return 8;
  end function;
  signal v : unsigned(7 downto 0) := x"01";
  signal c : std_logic := '0';
  signal n : integer := 8;
  signal o : unsigned(7 downto 0);
  signal first : integer;
begin
  o <= f(v, c);
  first <= first_set(v, n);
  process
    variable p : integer;
  begin
    wait for 1 ns;
    assert o = x"01" report "c = 0" severity failure;
    assert first = 0 report "first 0" severity failure;
    c <= '1';
    v <= x"10";
    wait for 1 ns;
    assert o = x"18" report "c = 1" severity failure;
    assert first = 4 report "first 4" severity failure;
    n <= 3;
    wait for 1 ns;
    assert first = 100 report "n = 3" severity failure;
    v <= x"00";
    n <= 8;
    wait for 1 ns;
    assert first = 8 report "none set" severity failure;
    p := first_set(x"20", n);
    assert p = 5 report "variable call" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
