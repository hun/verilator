-- DESCRIPTION: Verilator: multi-dimensional array type with an index constraint
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package p is
  subtype t_row is std_logic_vector(3 downto 0);
  type t_bits is array (natural range <>, natural range <>) of std_logic;
  type t_rows is array (natural range <>) of t_row;
  subtype t_4x4 is t_bits(3 downto 0, 3 downto 0);
end package p;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.p.all;

entity t is
  port (
    sel : in  std_logic_vector(1 downto 0);
    o   : out std_logic_vector(3 downto 0);  -- rows(sel)
    o2  : out std_logic_vector(3 downto 0);  -- bits(i, i)
    o3  : out std_logic                      -- quad(3, 1)
  );
end entity t;

architecture rtl of t is
  constant NROWS : natural := 4;
  signal bits : t_bits(NROWS - 1 downto 0, 3 downto 0)
    := (('0', '0', '1', '1'), ('1', '0', '1', '0'), ('1', '1', '1', '1'), ('0', '0', '0', '0'));
  signal rows : t_rows(NROWS - 1 downto 0) := (x"1", x"2", x"3", x"4");
  constant quad : t_4x4
    := (('0', '0', '1', '1'), ('1', '0', '1', '0'), ('1', '1', '1', '1'), ('0', '0', '0', '0'));
begin
  o <= rows(to_integer(unsigned(sel)));
  -- The diagonal of a matrix whose rows are written most significant index first
  gen : for i in 0 to NROWS - 1 generate
    o2(i) <= bits(i, i);
  end generate gen;
  o3 <= quad(3, 1);
end architecture rtl;
