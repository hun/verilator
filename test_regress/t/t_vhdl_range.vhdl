-- DESCRIPTION: Verilator: VHDL array ranges and index directions
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  generic (N : natural := 3);
  port (
    d     : in std_logic_vector(3 downto 0);
    a     : in std_logic_vector(0 to 3);
    o_d0  : out std_logic;
    o_d3  : out std_logic;
    o_a0  : out std_logic;
    o_a3  : out std_logic;
    o_sl  : out std_logic_vector(1 downto 0);
    o_sl2 : out std_logic_vector(1 downto 0);
    o_sl3 : out std_logic_vector(1 downto 0);
    o_sl4 : out std_logic_vector(1 downto 0);
    o_ca  : out std_logic_vector(3 downto 0);
    o_rr  : out std_logic;
    o_g   : out std_logic_vector(2**N - 1 downto 0);
    o_rng : out std_logic_vector(3 downto 0);
    o_av  : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  -- An ascending aggregate associates from the left index, the most significant
  constant CA : std_logic_vector(0 to 3) := (0 => '1', others => '0');
  -- The reverse range has the same bounds in the opposite direction
  signal rs : std_logic_vector(d'reverse_range);
begin
  o_d0 <= d(0);
  o_d3 <= d(3);
  o_a0 <= a(0);
  o_a3 <= a(3);
  -- A slice keeps the aggregate's own direction: a(1) is the left element
  o_sl <= a(1 to 2);
  -- A slice's left bound is its most significant element in either direction
  o_sl2 <= d(1 to 2);
  o_sl3 <= a(2 downto 1);
  o_sl4 <= d(2 downto 1);
  o_ca <= CA;
  rs <= "1010";
  o_rr <= rs(0);
  o_g <= (others => '1');

  process (a)
    variable rv : std_logic_vector(3 downto 0);
    variable av : std_logic_vector(0 to 3);
  begin
    rv := (a'range => '1');
    o_rng <= rv;
    av := (1 to 2 => '1', others => '0');
    o_av <= av;
  end process;
end;
