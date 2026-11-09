-- DESCRIPTION: Verilator: VHDL indexing a one-element array
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    idx : in integer;
    d : in std_logic;
    q : out std_logic;
    q2 : out std_logic;
    wide : out std_logic_vector(63 downto 0)
  );
end;

architecture rtl of t is
  constant store : std_logic_vector(0 downto 0) := "0";
  signal one : std_logic_vector(0 downto 0) := "1";
  signal mem : std_logic_vector(0 downto 0);
begin
  q <= store(idx) xor one(idx);

  process (idx, d)
    variable w : std_logic_vector(63 downto 0);
    variable xb : std_logic;
  begin
    w := x"FFFFFFFF00000000";
    xb := 'X';
    mem(idx) <= d;
    wide <= w;
  end process;

  q2 <= mem(0);
end;
