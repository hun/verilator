-- DESCRIPTION: Verilator: a VHDL testbench generates a clock and checks its design by itself
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity tb is
end entity tb;

architecture sim of tb is
  signal clk : std_logic := '0';
  signal q   : std_logic := '0';
begin
  -- A clock generator: the process repeats because its wait is a delay
  clk_proc : process
  begin
    clk <= not clk;
    wait for 5 ns;
  end process clk_proc;

  -- The design under test: a toggle flip-flop
  dff : process (clk)
  begin
    if rising_edge(clk) then
      q <= not q;
    end if;
  end process dff;

  -- At 22 ns three rising edges have happened (at 0, 10 and 20 ns), so q is high
  check : process
  begin
    wait for 22 ns;
    if q = '0' then
      report "q is low after three edges";
    else
      report "q is high after three edges";
    end if;
    std.env.finish;
    wait;
  end process check;
end architecture sim;