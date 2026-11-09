-- DESCRIPTION: Verilator: VHDL sibling for-generates sharing labels
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity bit_inv is
  port (
    x : in std_logic;
    y : out std_logic
  );
end;

architecture rtl of bit_inv is
begin
  y <= not x;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0);
    z : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
begin
  -- The label u1 is reused in two sibling generate blocks; the flattened
  -- names must still be unique.
  g1 : for i in 0 to 1 generate
    signal s : std_logic;
  begin
    u1 : entity work.bit_inv port map (x => a(i), y => s);
    y(i) <= s;
  end generate;

  g2 : for i in 0 to 1 generate
    signal s : std_logic;
  begin
    u1 : entity work.bit_inv port map (x => a(i + 2), y => s);
    y(i + 2) <= s;
  end generate;

  -- Positional association with an expression actual and a sliced output
  u3 : entity work.bit_inv port map (not a(0), z(0));
  u4 : entity work.bit_inv port map (x => not a(1), y => z(1));
end;
