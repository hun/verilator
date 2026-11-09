-- DESCRIPTION: Verilator: VHDL process splitting
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk  : in std_logic;
    clk2 : in std_logic;
    d    : in std_logic;
    a    : in std_logic;
    en   : in std_logic;
    last : out std_logic;
    comb : out std_logic;
    q1   : out std_logic;
    q2   : out std_logic;
    q3   : out std_logic;
    qv1  : out std_logic;
    qv2  : out std_logic;
    qe   : out std_logic;
    qm   : out std_logic;
    qr   : out std_logic;
    qr2  : out std_logic
  );
end;

architecture rtl of t is
begin
  -- Two clocked statements on the same edge: the last assignment wins
  process (clk) is
  begin
    if rising_edge(clk) then
      last <= '1';
    end if;
    if rising_edge(clk) then
      last <= d;
    end if;
  end process;

  -- A clocked statement mixed with an unclocked one
  process (clk, a) is
  begin
    if rising_edge(clk) then
      q1 <= d;
    end if;
    comb <= a;
  end process;

  -- Two clocks with disjoint targets
  process (clk, clk2) is
  begin
    if rising_edge(clk) then
      q2 <= d;
    end if;
    if rising_edge(clk2) then
      q3 <= not d;
    end if;
  end process;

  -- Two clocks with disjoint process variables
  process (clk, clk2) is
    variable v1 : std_logic := '0';
    variable v2 : std_logic := '0';
  begin
    if rising_edge(clk) then
      v1 := d;
      qv1 <= v1;
    end if;
    if rising_edge(clk2) then
      v2 := not d;
      qv2 <= v2;
    end if;
  end process;

  -- A later branch of a single-clock chain runs on the falling edge
  process (clk) is
  begin
    if rising_edge(clk) then
      qe <= d;
    elsif en = '1' then
      qe <= not d;
    end if;
  end process;

  -- A process variable carried across statements of a single-clock process
  process (clk) is
    variable m : std_logic := '0';
  begin
    if rising_edge(clk) then
      m := d;
    end if;
    qm <= m;
  end process;

  -- Both edges in one chain
  process (clk) is
  begin
    if rising_edge(clk) then
      qr <= d;
    elsif falling_edge(clk) then
      qr2 <= not d;
    end if;
  end process;
end;
