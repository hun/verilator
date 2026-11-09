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
    agg0 : out std_logic_vector(7 downto 0);
    agg1 : out std_logic_vector(7 downto 0);
    agg2 : out std_logic_vector(3 downto 0);
    agg3 : out std_logic_vector(3 downto 0);
    sq : out signed(7 downto 0);
    w : out integer;
    u4 : in std_ulogic_vector(3 downto 0);
    uo : out std_ulogic_vector(3 downto 0);
    eqf : out std_logic;
    eqt : out std_logic;
    lh : out std_logic_vector(3 downto 0);
    xz : out std_logic_vector(3 downto 0);
    ch : out std_logic;
    wide : out std_logic_vector(39 downto 0)
  );
end;

architecture rtl of t is
  constant HI : integer := 7;
  signal u : unsigned(3 downto 0);
  signal us : std_ulogic_vector(3 downto 0);

  function inv(x : std_ulogic_vector) return std_ulogic_vector is
  begin
    return not x;
  end;
begin
  u <= unsigned(a);
  us <= u4;
  uo <= inv(us);
  -- a shorter numeric_std literal operand is zero-extended, not an error
  eqf <= '1' when unsigned'("010") = "00" else '0';
  eqt <= '1' when unsigned'("000") = "00" else '0';
  -- a literal wider than 32 bits must not be truncated
  wide <= x"123456789A";
  -- weak and strong logic values map to their resolved levels
  lh <= "L1H0";
  xz <= "01X-";
  ch <= 'H';
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
  agg0 <= (0 => a(2), others => a(3));
  agg1 <= (7 downto 4 => a(0), others => a(1));
  agg2 <= (a(3), a(2), a(1), a(0));
  agg3 <= (3 => a(1), 0 => a(2), others => '0');
  -- a signed qualification must keep its signing through sra
  sq <= signed'(x"F0") sra 1;
  w <= to_integer(u);
end;
