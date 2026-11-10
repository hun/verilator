-- DESCRIPTION: Verilator: VHDL ports with an omitted mode
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity and2 is
  port (
    a, b : std_logic;  -- IEEE 1800-2023 17.2: an omitted mode is `in`
    y    : out std_logic
  );
end;

architecture rtl of and2 is
begin
  y <= a and b;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a  : std_logic;  -- omitted mode
    b  : in std_logic;
    y  : out std_logic;
    y2 : out std_logic
  );
end;

architecture rtl of t is
  component and2 is
    port (
      a : std_logic;  -- omitted mode
      b : std_logic;
      y : out std_logic
    );
  end component;
begin
  u1 : and2 port map (a => a, b => b, y => y);
  u2 : entity work.and2 port map (a => b, b => a, y => y2);
end;
