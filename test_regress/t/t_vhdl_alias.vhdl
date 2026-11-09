-- DESCRIPTION: Verilator: VHDL object aliases through GHDL XML
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package alp is
  constant c0 : integer := 2;
  alias c0a is c0;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.alp.all;

entity t is
  port (
    din   : in std_logic;
    y     : out std_logic;
    hi_o  : out std_logic_vector(1 downto 0);
    n     : out integer range 0 to 7;
    din8 : in std_logic_vector(7 downto 0);
    lo   : out std_logic_vector(3 downto 0);
    lo2  : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
  signal s : std_logic_vector(3 downto 0);
  alias sl : std_logic is s(1);
  alias sl2 is sl;
  alias sv : std_logic_vector(3 downto 0) is s;
  alias sh : std_logic_vector(1 downto 0) is s(3 downto 2);
  signal s8 : std_logic_vector(7 downto 0);
  signal w8 : std_logic_vector(7 downto 0);
  alias slv6 : std_logic_vector(7 downto 2) is s8(6 downto 1);
  alias slv4 : std_logic_vector(3 downto 0) is slv6(6 downto 3);
  alias wlv6 : std_logic_vector(7 downto 2) is w8(6 downto 1);
  alias wlv4 : std_logic_vector(3 downto 0) is wlv6(6 downto 3);
begin
  sv <= din & "010";
  y <= sl2;
  hi_o <= sh;
  n <= c0a;
  s8 <= din8;
  -- Read and write through aliases of aliases of slices
  lo <= slv4;
  wlv4 <= din8(3 downto 0);
  w8(7) <= din8(7);
  w8(1 downto 0) <= din8(1 downto 0);
  lo2 <= wlv4;
end;
