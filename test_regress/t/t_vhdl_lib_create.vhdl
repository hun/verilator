-- DESCRIPTION: Verilator: VHDL counter built as a DPI library with --lib-create
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dut is
  port (
    clk : in std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of dut is
  signal s : unsigned(3 downto 0) := (others => '0');
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      s <= s + 1;
    end if;
  end process;
  q <= std_logic_vector(s);
end;
