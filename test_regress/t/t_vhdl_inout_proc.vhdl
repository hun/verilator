-- DESCRIPTION: Verilator: VHDL inout port driven from a clocked process
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity drv is
  port (
    clk : in std_logic;
    en  : in std_logic;
    d   : in std_logic;
    io  : inout std_logic
  );
end;

architecture rtl of drv is
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      if en = '1' then
        io <= d;
      else
        io <= 'Z';
      end if;
    end if;
  end process;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    en  : in std_logic;
    d   : in std_logic;
    y   : out std_logic
  );
end;

architecture rtl of t is
  signal bus_s : std_logic;
begin
  u : entity work.drv port map (clk => clk, en => en, d => d, io => bus_s);
  y <= bus_s;
end;
