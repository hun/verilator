-- DESCRIPTION: Verilator: a constant of an unconstrained array of records
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- poc's configuration lists boards as records of strings in a constant whose array type
-- states no range: the aggregate supplies it, one element per association from the index
-- subtype's low bound (IEEE 1076-2008 9.3.3.2). The element type is a record whose members
-- are constrained subtypes, and an element is selected through the array index.

package pkg is
  subtype s8 is string(1 to 8);
  type t_rec is record
    tag : s8;
    val : std_logic_vector(3 downto 0);
  end record;
  subtype idx_c is natural range 0 to 3;
  type t_arr is array (natural range <>) of t_rec;

  constant una_c : t_arr := ((tag => "first   ", val => x"1"),
                             (tag => "second  ", val => x"2"),
                             (tag => "third   ", val => x"3"));
  constant one_c : t_rec := (tag => "chosen  ", val => x"7");
  constant oth_c : t_arr(idx_c) := (others => one_c);
  constant len_c : natural := una_c'length;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.pkg.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out std_logic_vector(3 downto 0);
    o2  : out std_logic_vector(3 downto 0);
    len : out std_logic_vector(1 downto 0)
  );
end entity;

architecture rtl of t is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o <= una_c(1).val;
      o2 <= oth_c(3).val;
      len <= std_logic_vector(to_unsigned(len_c, 2));
    end if;
  end process;
end architecture;
