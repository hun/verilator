-- DESCRIPTION: Verilator: two processes declare a variable of the same name
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    n   : in  integer;
    o_a : out integer;
    o_b : out integer
  );
end entity t;

architecture rtl of t is
begin
  proc_a : process (n)
    variable r : integer := 0;
  begin
    r := n;
    o_a <= r;
  end process proc_a;

  proc_b : process (n)
    variable r : integer := 0;
  begin
    r := n + 1;
    o_b <= r;
  end process proc_b;
end architecture rtl;
