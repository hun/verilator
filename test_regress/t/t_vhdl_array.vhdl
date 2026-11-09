-- DESCRIPTION: Verilator: VHDL arrays of multi-bit elements
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a : in std_logic_vector(7 downto 0);
    i : in integer;
    y : out std_logic_vector(7 downto 0);
    z : out std_logic_vector(7 downto 0);
    cat : out std_logic_vector(23 downto 0);
    sh1 : out std_logic_vector(7 downto 0);
    sh2 : out std_logic_vector(7 downto 0);
    nout : out std_logic_vector(7 downto 0);
    rflag : out std_logic;
    rdata : out std_logic;
    o0 : out std_logic_vector(7 downto 0);
    o1 : out std_logic_vector(7 downto 0);
    esel : out std_logic;
    agg : out std_logic_vector(15 downto 0);
    d : out std_logic_vector(7 downto 0);
    sumout : out std_logic_vector(9 downto 0);
    neg : out signed(7 downto 0);
    slt : out std_logic;
    len : out integer;
    lo : out integer;
    hi : out integer
  );
end;

architecture rtl of t is
  type mem_t is array (0 to 3) of std_logic_vector(7 downto 0);
  type dmem_t is array (3 downto 0) of std_logic_vector(7 downto 0);
  type smem_t is array (0 to 1) of signed(7 downto 0);
  subtype byte_t is std_logic_vector(7 downto 0);
  type nmem_t is array (0 to 1) of byte_t;
  type rec_t is record
    flag : std_logic;
    data : std_logic_vector(3 downto 0);
  end record;
  type rmem_t is array (0 to 1) of rec_t;
  type inner_t is array (0 to 1) of std_logic_vector(7 downto 0);
  type outer_t is array (0 to 1) of inner_t;
  type state_t is (idle, run, stop);
  type smem2_t is array (0 to 1) of state_t;
  signal mem : mem_t;
  signal dmem : dmem_t;
  signal aggsig : mem_t;
  signal smem : smem_t;
  signal sh : mem_t;
  signal nmem : nmem_t;
  signal rmem : rmem_t;
  signal outer : outer_t;
  signal states : smem2_t;
begin
  mem(0) <= a;
  mem(1) <= std_logic_vector(unsigned(a) + 1);
  mem(2) <= std_logic_vector(unsigned(a) + 2);
  mem(3) <= std_logic_vector(unsigned(a) + 3);
  -- A descending array maps its lowest index to the least significant element
  dmem(3) <= a;
  dmem(2) <= std_logic_vector(unsigned(a) + 1);
  dmem(1) <= std_logic_vector(unsigned(a) + 2);
  dmem(0) <= std_logic_vector(unsigned(a) + 3);
  aggsig <= (others => a);
  smem(0) <= signed(a);
  smem(1) <= -signed(a);

  y <= mem(1);
  sh(0) <= a;
  sh(3) <= a;
  -- A slice of an array of vectors stays an array of vectors
  sh(1 to 2) <= mem(2 to 3);
  sh1 <= sh(1);
  sh2 <= sh(2);
  -- A named element subtype and an array of records
  nmem(0) <= a;
  nmem(1) <= std_logic_vector(unsigned(a) + 1);
  nout <= nmem(1);
  rmem(0).flag <= a(0);
  rmem(0).data <= a(4 downto 1);
  rmem(1) <= rmem(0);
  rflag <= rmem(1).flag;
  rdata <= rmem(1).data(0);
  -- Nested arrays of vectors keep the full element width
  outer(0)(0) <= a;
  outer(0)(1) <= std_logic_vector(unsigned(a) + 1);
  outer(1)(0) <= std_logic_vector(unsigned(a) + 2);
  outer(1)(1) <= std_logic_vector(unsigned(a) + 3);
  o0 <= outer(1)(1);
  o1 <= outer(0)(0);
  -- Enum elements keep the enum's own width
  states(0) <= idle;
  states(1) <= stop;
  esel <= '1' when states(1) = stop else '0';
  z <= mem(i);
  cat <= mem(3) & mem(2) & mem(1);
  agg <= aggsig(1) & aggsig(0);
  d <= dmem(2);
  neg <= smem(1);
  slt <= '1' when smem(0) < 0 else '0';
  len <= mem'length;
  lo <= mem'low;
  hi <= mem'high;

  process (mem)
    variable sum : unsigned(9 downto 0);
  begin
    sum := (others => '0');
    for j in mem'range loop
      sum := sum + unsigned(mem(j));
    end loop;
    sumout <= std_logic_vector(sum);
  end process;
end;
