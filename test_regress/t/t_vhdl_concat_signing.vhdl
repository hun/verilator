-- DESCRIPTION: Verilator: a bit-string literal beside an unsigned keeps its signing
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- poc's arith_FirstOne extends a vector with a zero through a concatenation and then
-- adds: `("0" & unsigned(not Request)) + 1`. The literal must take the numeric_std
-- operand's type, or the sum is arithmetic on a plain array.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a  : in  std_logic_vector(7 downto 0);
    o  : out std_logic_vector(8 downto 0);
    cy : out std_logic
  );
end entity;

architecture rtl of t is
  signal sum : unsigned(8 downto 0);
begin
  sum <= ("0" & unsigned(a)) + 1;
  o <= std_logic_vector(sum);
  cy <= sum(8);
end architecture;
