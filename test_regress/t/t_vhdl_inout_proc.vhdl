-- DESCRIPTION: Verilator: VHDL inout port driven from a clocked process
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
