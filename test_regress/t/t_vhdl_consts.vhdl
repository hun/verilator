-- DESCRIPTION: Verilator: VHDL package constants and generic defaults
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package pkg is
  constant W : integer := 4;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.pkg.all;

entity t is
  generic (G : integer := 8);
  port (
    a : in std_logic_vector(W - 1 downto 0);
    b : in std_logic_vector(G - 1 downto 0);
    n : out integer;
    y : out std_logic_vector(G - 1 downto 0);
    m0 : out std_logic;
    mhi : out std_logic_vector(3 downto 0);
    ar0 : out std_logic_vector(3 downto 0);
    s1o : out std_logic_vector(3 downto 0);
    s2o : out std_logic_vector(3 downto 0);
    io : out integer
  );
end;

architecture rtl of t is
  constant C : integer := 2;
  constant MASK : std_logic_vector(7 downto 0) := x"3c";
  type arr_t is array (0 to 1) of std_logic_vector(3 downto 0);
  constant AR : arr_t := (x"1", x"2");
  -- GHDL emits the follow-on identifiers as reference declarations; their
  -- default value is a separate ref node
  signal s1, s2 : std_logic_vector(3 downto 0) := x"5";
  signal i1, i2 : integer := 3;
begin
  n <= C + W;
  y <= b;
  -- Indexing and slicing a constant resolves through its declared value
  m0 <= MASK(0);
  mhi <= MASK(7 downto 4);
  ar0 <= AR(0);
  s1o <= s1;
  s2o <= s2;
  io <= i1 + i2;
end;
