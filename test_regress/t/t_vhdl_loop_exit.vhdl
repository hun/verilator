-- DESCRIPTION: Verilator: VHDL exit and next in static loops
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    v : in std_logic_vector(7 downto 0);
    idx : out integer;
    found : out std_logic;
    cnt : out integer;
    pairs : out integer;
    latch : out integer
  );
end;

architecture rtl of t is
begin
  -- Priority encoder: first set bit wins, later iterations are dead
  process (v)
    variable r : integer := 0;
    variable f : std_logic := '0';
  begin
    for i in 0 to 7 loop
      if v(i) = '1' then
        r := i;
        f := '1';
        exit;
        r := 99;  -- unreachable
      end if;
    end loop;
    idx <= r;
    found <= f;
  end process;

  -- next skips the rest of the current iteration only
  process (v)
    variable n : integer := 0;
  begin
    n := 0;
    for i in 0 to 7 loop
      next when v(i) = '0';
      n := n + 1;
    end loop;
    cnt <= n;
  end process;

  -- An inner-loop exit must not skip the outer loop
  process (v)
    variable p : integer := 0;
  begin
    p := 0;
    for i in 0 to 3 loop
      for j in 0 to 1 loop
        if v(i * 2 + j) = '1' then
          p := p + 1;
          exit;
        end if;
      end loop;
    end loop;
    pairs <= p;
  end process;

  -- The exit decision must be latched; d changes after the check
  process (v)
    variable d : std_logic := '0';
    variable r : integer := 0;
  begin
    r := 0;
    d := '0';
    for i in 0 to 3 loop
      if d = '1' then
        r := i;
        exit;
      end if;
      d := '1';
    end loop;
    latch <= r;
  end process;
end;
