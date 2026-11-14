-- DESCRIPTION: Verilator: a VHDL testbench generates a clock with a delayed concurrent assignment
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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