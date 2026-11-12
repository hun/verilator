-- DESCRIPTION: Verilator: VHDL 'Z' logic literal lowering
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    en : in std_logic;
    d : in std_logic;
    qz : out std_logic;
    qv : out std_logic_vector(3 downto 0);
    qo : out std_logic_vector(2 downto 0)
  );
end;

architecture rtl of t is
  signal s : std_logic;
  signal v : std_logic_vector(3 downto 0);
  signal u : std_logic_vector(2 downto 0);
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      if en = '1' then
        s <= d;
      else
        s <= 'Z';
      end if;
      v <= "0Z10";
      u <= (others => 'Z');
    end if;
  end process;
  qz <= s;
  qv <= v;
  qo <= u;
end;
