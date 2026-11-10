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
    sel : in std_logic_vector(1 downto 0);
    y : out std_logic_vector(7 downto 0);
    z : out std_logic_vector(1 downto 0);
    ay : out std_logic_vector(7 downto 0);
    q : out std_logic_vector(7 downto 0);
    vy : out std_logic_vector(7 downto 0);
    fy : out std_logic_vector(7 downto 0);
    tq : out std_logic_vector(15 downto 0);
    ry : out std_logic_vector(31 downto 0);
    ary : out std_logic_vector(0 to 31);
    wy : out std_logic_vector(31 downto 0)
  );
end;

architecture rtl of t is
  signal mem : std_logic_vector(31 downto 0);
  signal amem : std_logic_vector(0 to 31);
  signal ram : std_logic_vector(31 downto 0) := (others => '0');
  signal wram : std_logic_vector(31 downto 0);

  function swap (x : std_logic_vector(15 downto 0)) return std_logic_vector is
  begin
    return x(7 downto 0) & x(15 downto 8);
  end function;
begin
  mem <= a & a;
  amem <= a & a;

  -- Attribute ranges as slice bounds, reading descending and ascending arrays
  ry <= mem(mem'range);
  ary <= amem(amem'range);
  -- Attribute range as an assignment target
  wram(wram'range) <= a & a;
  wy <= wram;

  -- Descending slices with a dynamic low bound
  y <= mem(i * 8 + 7 downto i * 8);
  z <= mem(i * 2 + 1 downto i * 2);
  -- Ascending slice with a dynamic low bound
  ay <= amem(i * 8 to i * 8 + 7);
  fy <= swap(a)(7 downto 0);

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

  -- Slice width recovered from the linear relation between two variables
  process (i, a)
    variable lo, hi : natural;
  begin
    lo := (i mod 2) * 8;
    hi := lo + 7;
    vy <= a(hi downto lo);
  end process;

  -- Variable slice as a write target inside process(all)
  process (all)
    variable t : std_logic_vector(15 downto 0);
    variable lo, hi : natural;
  begin
    t := a;
    for k in 0 to 1 loop
      if sel(k) = '1' then
        lo := k * 8;
        hi := lo + 7;
        t(hi downto lo) := din;
      end if;
    end loop;
    tq <= t;
  end process;
end;
