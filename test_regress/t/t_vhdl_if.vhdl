-- DESCRIPTION: Verilator: VHDL if/elsif/else, GHDL XML frontend
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic;
    b : in std_logic;
    y : out std_logic
  );
end;

architecture rtl of t is
begin
  process (a, b) is
  begin
    if a = '1' then
      y <= b;
    elsif b = '1' then
      y <= '0';
    else
      y <= '1';
    end if;
  end process;
end;
