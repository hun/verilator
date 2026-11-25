-- DESCRIPTION: Verilator: a function loop whose condition stops folding part way through
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The first iterations fold and write the locals; the elsif at i = 4 depends on a signal,
-- so the loop is unrolled after all and the writes must not count twice

entity t is end entity;
architecture a of t is
  function f_dyn(n : integer) return integer is
    variable acc : integer := 0;
    variable b : bit_vector(0 to 7) := (others => '0');
  begin
    for i in 0 to 7 loop
      if i < 4 then
        b(i) := '1';
        acc := acc + i;
      elsif i < n then
        acc := acc + 10 * i;
        if b(i - 4) = '1' then
          acc := acc + 1;
        end if;
      end if;
    end loop;
    return acc;
  end function;
  -- The outer loop is unrolled; the inner one folds but exits the outer one
  function f_out(n : integer) return integer is
    variable acc : integer := 0;
  begin
    outer : for i in 0 to 3 loop
      if i < n then
        acc := acc + 100;
      end if;
      for j in 0 to 3 loop
        exit outer when i = 2 and j = 1;
        acc := acc + 1;
      end loop;
    end loop;
    return acc;
  end function;
  signal s : integer := 3;
  signal r : integer;
  signal r2 : integer;
begin
  r <= f_dyn(s);
  r2 <= f_out(s);
  process
    variable v : integer;
  begin
    wait for 1 ns;
    v := f_dyn(s);
    assert r = 6 and v = 6 and r2 = 309 report "n = 3" severity failure;
    s <= 7;
    wait for 1 ns;
    v := f_dyn(s);
    assert r = 159 and v = 159 report "n = 7" severity failure;
    s <= 1;
    wait for 1 ns;
    assert r2 = 109 report "n = 1" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
