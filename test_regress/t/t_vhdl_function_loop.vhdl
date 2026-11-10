-- DESCRIPTION: Verilator: VHDL function loops, exit/next and local updates
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a     : in  std_logic_vector(7 downto 0);
    rev   : out std_logic_vector(7 downto 0);
    cnt   : out integer;
    first : out integer;
    outer : out integer;
    c     : out std_logic_vector(7 downto 0);
    k     : out integer
  );
end;

architecture rtl of t is
  function rev8(v : std_logic_vector(7 downto 0)) return std_logic_vector is
    variable r : std_logic_vector(7 downto 0) := (others => '0');
  begin
    for i in 0 to 7 loop
      r(7 - i) := v(i);
    end loop;
    return r;
  end rev8;

  function popcount(v : std_logic_vector) return integer is
    variable cntv : integer := 0;
  begin
    for i in v'range loop
      if v(i) = '1' then
        cntv := cntv + 1;
      end if;
    end loop;
    return cntv;
  end popcount;

  function firstbit(v : std_logic_vector) return integer is
    alias av : std_logic_vector(v'length - 1 downto 0) is v;
  begin
    for i in av'reverse_range loop
      if av(i) = '1' then
        return i;
      end if;
    end loop;
    return -1;
  end firstbit;

  function exit_outer(v : std_logic_vector) return integer is
    variable r : integer := 0;
  begin
    L1: for i in 0 to 7 loop
      for j in 0 to 1 loop
        r := r + 1;
        exit L1 when v(i) = '1';
      end loop;
    end loop;
    return r;
  end exit_outer;

  constant KONST : integer := popcount("10110011");
  constant CREV : std_logic_vector(7 downto 0) := rev8("10110011");
begin
  rev <= rev8(a);
  cnt <= popcount(a);
  first <= firstbit(a);
  outer <= exit_outer(a);
  c <= CREV;
  k <= KONST;
end;
