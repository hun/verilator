-- DESCRIPTION: Verilator: a VHDL testbench generates a clock with a delayed concurrent assignment
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
begin
  -- A clock generator: a concurrent assignment that drives the target after a delay
  clk <= not clk after 5 ns;

  -- At 22 ns the clock has toggled at 5, 10, 15 and 20 ns, so it is low again
  check : process
  begin
    wait for 22 ns;
    if clk = '1' then
      report "clk is high at 22 ns";
    else
      report "clk is low at 22 ns";
    end if;
    std.env.finish;
    wait;
  end process check;
end architecture sim;