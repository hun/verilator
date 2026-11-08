-- DESCRIPTION: Verilator: VHDL static attributes and length-based loop
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a   : in std_logic_vector(3 downto 0);
    y   : out std_logic_vector(3 downto 0);
    n   : out integer;
    h   : out integer;
    asc : out boolean
  );
end;

architecture rtl of t is
begin
  n <= a'length;
  h <= a'high;
  asc <= a'ascending;

  process (a) is
  begin
    for i in 0 to a'length - 1 loop
      y(i) <= a(i);
    end loop;
  end process;
end;
