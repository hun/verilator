-- DESCRIPTION: Verilator: 2**n with a run-time exponent
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A power of two with an exponent that is not static is a shift, so a design that turns
-- a counter or a generic-derived width into a decoder size lowers. A static exponent
-- keeps the unrolled multiplication chain.

library ieee;
use ieee.std_logic_1164.all;

entity p2 is
  port (
    clk : in  std_logic;
    rst : in  std_logic;
    o   : out integer;
    s   : out integer
  );
end entity;

architecture rtl of p2 is
  signal count : integer range 0 to 20 := 0;
begin
  p_count : process (clk) is
  begin
    if rising_edge(clk) then
      if rst = '1' then
        count <= 0;
      elsif count /= 20 then
        count <= count + 1;
      end if;
    end if;
  end process;

  o <= 2 ** count;
  s <= 2 ** 6 + 2 ** 0;
end architecture;
