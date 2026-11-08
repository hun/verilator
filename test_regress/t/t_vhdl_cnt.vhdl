-- DESCRIPTION: Verilator: VHDL counter, GHDL XML frontend
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk : in std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  signal s : unsigned(3 downto 0) := (others => '0');
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      s <= s + 1;
      q <= std_logic_vector(s);
    end if;
  end process;
end;
