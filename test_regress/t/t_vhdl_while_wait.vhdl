-- DESCRIPTION: Verilator: a while loop with a wait and a run-time test, and a bare loop with exit and next
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- lim drops to 2 at 4 ns, so the while loop stops after two iterations at 5 ns.
-- The bare loop then runs at 6..11 ns: next skips the k = 2 addition, exit ends it at k = 6.

entity t is end entity;
architecture a of t is
  signal lim : integer := 5;
  signal nout, kout, accout : integer := 0;
  signal done : bit := '0';
begin
  lim_drv : process begin
    wait for 4 ns;
    lim <= 2;
    wait;
  end process;

  proc : process
    variable n : integer := 0;
    variable k : integer := 0;
    variable acc : integer := 0;
  begin
    wait for 1 ns;
    while n < lim loop
      wait for 2 ns;
      n := n + 1;
    end loop;
    nout <= n;
    loop
      wait for 1 ns;
      k := k + 1;
      next when k = 2;
      acc := acc + k;
      exit when k = 6;
    end loop;
    kout <= k;
    accout <= acc;
    done <= '1';
    wait;
  end process;

  check : process begin
    wait until done = '1';
    assert nout = 2 report "while" severity failure;
    assert kout = 6 report "loop exit" severity failure;
    assert accout = 19 report "next" severity failure;
    report "PASS";
    std.env.finish;
  end process;
end architecture;
