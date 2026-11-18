-- DESCRIPTION: Verilator: VHDL inout ports connected to one tristate bus
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity drv is
  port (
    en : in std_logic;
    d  : in std_logic;
    io : inout std_logic
  );
end;

architecture rtl of drv is
begin
  io <= d when en = '1' else 'Z';
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    en1 : in std_logic;
    en2 : in std_logic;
    d1  : in std_logic;
    d2  : in std_logic;
    y   : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
  signal bus_s : std_logic;
begin
  -- Two instances drive the same resolved signal. VHDL would resolve a
  -- conflicting 0 and 1 to 'X'; the two-state model resolves it to 1, and the
  -- test pins that documented divergence.
  u1 : entity work.drv port map (en => en1, d => d1, io => bus_s);
  u2 : entity work.drv port map (en => en2, d => d2, io => bus_s);
  y <= bus_s & bus_s;
end;
