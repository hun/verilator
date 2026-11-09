-- DESCRIPTION: Verilator: VHDL non-static slices with static widths
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    we : in std_logic;
    waddr : in integer;
    din : in std_logic_vector(7 downto 0);
    a : in std_logic_vector(15 downto 0);
    i : in integer;
    y : out std_logic_vector(7 downto 0);
    z : out std_logic_vector(1 downto 0);
    ay : out std_logic_vector(7 downto 0);
    q : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
  signal mem : std_logic_vector(31 downto 0);
  signal amem : std_logic_vector(0 to 31);
  signal ram : std_logic_vector(31 downto 0) := (others => '0');
begin
  mem <= a & a;
  amem <= a & a;

  -- Descending slices with a dynamic low bound
  y <= mem(i * 8 + 7 downto i * 8);
  z <= mem(i * 2 + 1 downto i * 2);
  -- Ascending slice with a dynamic low bound
  ay <= amem(i * 8 to i * 8 + 7);

  -- Dynamic slice as an assignment target
  process (clk) is
  begin
    if rising_edge(clk) then
      if we = '1' then
        ram(waddr * 8 + 7 downto waddr * 8) <= din;
      end if;
    end if;
  end process;
  q <= ram(i * 8 + 7 downto i * 8);
end;
