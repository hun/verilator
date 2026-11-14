-- DESCRIPTION: Verilator: a slice whose bounds come from the prefix's own attribute
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
end entity t;

architecture sim of t is
  signal o : std_logic_vector(3 downto 0);
  signal s : std_logic_vector(7 downto 0) := "10110010";
  signal p : std_logic_vector(2 downto 0);
begin
  -- The bound is the prefix's own 'left, so the range needs no scope lookup
  o <= s(s'left downto 4);
  -- A bound with arithmetic around the attribute folds the same way
  p <= s(s'left-1 downto 4);

  check : process
  begin
    wait for 1 ns;
    if o = "1011" then
      report "o is 1011";
    else
      report "o is wrong";
    end if;
    if p = "011" then
      report "p is 011";
    else
      report "p is wrong";
    end if;
    std.env.finish;
    wait;
  end process check;
end architecture sim;