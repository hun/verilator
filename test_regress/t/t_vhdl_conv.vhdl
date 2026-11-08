-- DESCRIPTION: Verilator: VHDL numeric_std conversion builtins
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  generic (N : integer := 4);
  port (
    a : in std_logic_vector(3 downto 0);
    b : in std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0);
    z : out std_logic_vector(3 downto 0);
    v : out std_logic_vector(7 downto 0);
    q : out std_logic_vector(3 downto 0);
    r : out std_logic_vector(7 downto 0);
    t2 : out std_logic_vector(7 downto 0);
    o0 : out std_logic_vector(7 downto 0);
    o1 : out std_logic_vector(7 downto 0);
    ob : out std_logic_vector(7 downto 0);
    sq : out signed(7 downto 0);
    w : out integer
  );
end;

architecture rtl of t is
  constant HI : integer := 7;
  signal u : unsigned(3 downto 0);
begin
  u <= unsigned(a);
  -- resize of an unsigned value zero-extends; the width is a static expression
  y <= std_logic_vector(resize(u, N + 4));
  -- static to_signed folds to an aggregate with a literal origin
  z <= std_logic_vector(resize(to_signed(-1, N), N));
  -- resize of a signed value sign-extends
  v <= std_logic_vector(resize(to_signed(-1, N), N + 4));
  -- slice bounds are static expressions rather than literals
  q <= b(HI downto N);
  -- qualified bit-string literals
  r <= std_logic_vector'(x"0f");
  t2 <= b"10101010";
  -- array aggregates
  o0 <= (others => '0');
  o1 <= (others => '1');
  ob <= (others => b(0));
  -- a signed qualification must keep its signing through sra
  sq <= signed'(x"F0") sra 1;
  w <= to_integer(u);
end;
