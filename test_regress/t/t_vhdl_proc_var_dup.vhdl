-- DESCRIPTION: Verilator: two processes declare a variable of the same name
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
end entity t;

architecture rtl of t is
  signal n : integer := 41;
  signal a : integer;
  signal b : integer;
begin
  -- CHECK values: each process has its own variable of the same name, so neither
  -- assignment can see the other process's value
  proc_a : process (n)
    variable r : integer := 0;
  begin
    r := n;
    a <= r;
  end process proc_a;

  proc_b : process (n)
    variable r : integer := 0;
  begin
    r := n + 1;
    b <= r;
  end process proc_b;

  check : process
  begin
    wait for 1 ns;
    if a = 41 and b = 42 then
      report "vars are 41 42";
    else
      report "vars are wrong";
    end if;
    std.env.finish;
    wait;
  end process check;
end architecture rtl;
