-- DESCRIPTION: Verilator: an asynchronous reset flip-flop clears without a clock edge
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk   : in  std_logic;
    rstn  : in  std_logic;
    cnt_o : out unsigned(3 downto 0)
  );
end entity t;

architecture rtl of t is
  signal cnt : unsigned(3 downto 0);
begin
  process (rstn, clk) is
  begin
    if (rstn = '0') then
      cnt <= (others => '0');
    elsif rising_edge(clk) then
      cnt <= cnt + 1;
    end if;
  end process;
  cnt_o <= cnt;
end architecture rtl;
