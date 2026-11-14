-- DESCRIPTION: Verilator: a VHDL testbench reports and ends the simulation by itself
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity tb is
end entity tb;

architecture sim of tb is
  signal x : std_logic := '1';
begin
  -- A testbench reports what it observed and ends the run, so no C++ driver is needed
  check : process
  begin
    wait for 5 ns;
    if x = '1' then
      report "x is high";
    else
      report "x is low";
    end if;
    report "tb done";
    std.env.finish;
    wait;
  end process check;
end architecture sim;