-- DESCRIPTION: Verilator: VHDL wait-until clock processes
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    d   : in std_logic;
    qp  : out std_logic;
    qf  : out std_logic;
    qe  : out std_logic;
    qb  : out std_logic;
    qv  : out std_logic
  );
end;

architecture rtl of t is
begin
  process is
  begin
    wait until rising_edge(clk);
    qp <= d;
  end process;

  process is
  begin
    wait until falling_edge(clk);
    qf <= d;
  end process;

  process is
  begin
    wait until clk'event and clk = '1';
    qe <= d;
  end process;

  -- A process loops forever: the pre-wait statement runs again after the body, so the
  -- last assignment of the iteration is the one before the next wait
  process is
  begin
    qb <= '0';
    wait until rising_edge(clk);
    qb <= '1';
  end process;

  process is
    variable v : std_logic := '0';
  begin
    v := '1';
    wait until rising_edge(clk);
    v := d;
    qv <= v;
  end process;
end;
