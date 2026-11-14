-- DESCRIPTION: Verilator: two processes declare a variable of the same name
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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
