-- DESCRIPTION: Verilator: VHDL clock edge idioms
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    rst : in std_logic;
    en : in std_logic;
    d : in std_logic;
    q : out std_logic;
    qg : out std_logic;
    qp : out std_logic;
    qn : out std_logic;
    qc : out std_logic;
    qf : out std_logic;
    qr : out std_logic;
    qe : out std_logic
  );
end;

architecture rtl of t is
  constant EDGE : std_logic := '1';
  signal r : std_logic;
begin
  process (clk) is
  begin
    if clk'event and clk = '1' then
      q <= d;
    end if;
  end process;

  -- A level enable conjoined with the clock edge
  process (clk) is
  begin
    if en = '1' and rising_edge(clk) then
      qg <= d;
    end if;
  end process;

  -- Parenthesized conjunction
  process (clk) is
  begin
    if en = '1' and (rising_edge(clk)) then
      qp <= d;
    end if;
  end process;

  -- Event attribute with an inequality level test
  process (clk) is
  begin
    if clk'event and clk /= '0' then
      qn <= d;
    end if;
  end process;

  -- Event attribute compared with a std_logic constant
  process (clk) is
  begin
    if clk'event and clk = EDGE then
      qc <= d;
    end if;
  end process;

  process (clk) is
  begin
    if falling_edge(clk) then
      qf <= d;
    end if;
  end process;

  -- Asynchronous level reset before the clock branch
  process (clk, rst) is
  begin
    if rst = '1' then
      r <= '0';
    elsif rising_edge(clk) then
      r <= d;
    end if;
  end process;
  qr <= r;

  -- A conditional concurrent assignment gated by a clock edge
  qe <= '0' when rst = '1' else d when rising_edge(clk);
end;
