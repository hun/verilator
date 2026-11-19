-- DESCRIPTION: Verilator: a discrete subtype as an array index constraint
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- open-logic sizes its packet FIFO's pointers with `unsigned(Addr_c)`, where Addr_c is a
-- subtype of integer. The subtype's range is the vector's index range.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a : out std_logic_vector(3 downto 0);
    b : out unsigned(3 downto 0);
    c : out std_logic_vector(1 to 2)
  );
end entity;

architecture rtl of t is
  subtype R_c is integer range 3 downto 0;
  subtype Up_c is integer range 1 to 2;
  constant C1_c : std_logic_vector(R_c) := "1010";
  signal S : unsigned(R_c) := "0101";
  constant C2_c : std_logic_vector(Up_c) := "11";
begin
  a <= C1_c;
  b <= S;
  c <= C2_c;
end architecture;
