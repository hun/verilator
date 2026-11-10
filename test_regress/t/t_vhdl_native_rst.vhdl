-- DESCRIPTION: Verilator: VHDL async reset and integer counter, native frontend
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    rst : in std_logic;
    q   : out integer range 0 to 3
  );
end;

architecture rtl of t is
  signal cnt : integer range 0 to 3 := 0;
begin
  process (clk, rst) is
  begin
    if rst = '1' then
      cnt <= 0;
    elsif rising_edge(clk) then
      if cnt = 3 then
        cnt <= 0;
      else
        cnt <= cnt + 1;
      end if;
    end if;
  end process;
  q <= cnt;
end;
