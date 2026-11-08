-- DESCRIPTION: Verilator: VHDL synchronous reset, GHDL XML frontend
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
    d   : in std_logic;
    q   : out std_logic
  );
end;

architecture rtl of t is
begin
  -- rst is NOT in the sensitivity list: the reset is synchronous
  process (clk) is
  begin
    if rst = '1' then
      q <= '1';
    elsif rising_edge(clk) then
      q <= d;
    end if;
  end process;
end;
