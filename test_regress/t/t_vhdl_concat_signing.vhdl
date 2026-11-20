-- DESCRIPTION: Verilator: a bit-string literal beside an unsigned keeps its signing
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
