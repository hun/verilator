-- DESCRIPTION: Verilator: statements after a passing severity failure assert
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The assert condition reads x, which the statements after it change: they must still run

entity t is end entity;

architecture a of t is
  signal done : bit := '0';
  signal cnt : integer := 0;
begin
  process
    variable x : real;
    variable i : integer;
  begin
    x := 0.3;
    assert x + 1.0 < 2.0 report "first" severity failure;
    x := 1.5;
    cnt <= cnt + 1;
    i := 3;
    assert i + 1 < 5 report "second" severity failure;
    i := 9;
    cnt <= cnt + 10;
    wait for 1 ns;
    done <= '1';
    wait;
  end process;

  process
  begin
    wait until done = '1';
    assert cnt = 10 report "statements after the assert did not run" severity failure;
    report "PASS";
    std.env.finish;
  end process;
end architecture;
