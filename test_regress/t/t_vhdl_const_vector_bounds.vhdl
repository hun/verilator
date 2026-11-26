-- DESCRIPTION: Verilator: VHDL unconstrained vector constant takes its bounds from its value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  generic (
    a : std_logic_vector(7 downto 0) := x"ab";
    b : std_logic_vector(7 downto 0) := x"9e"
  );
  port (
    cat_o : out std_logic_vector(15 downto 0);
    len_o : out integer;
    top_o : out std_logic;
    low_o : out std_logic;
    sum_o : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
  -- The constants have no constraint of their own (IEEE 1076-2008 5.3.2.2)
  constant c : std_logic_vector := a & b;
  constant d : unsigned := unsigned(a) + 1;
begin
  cat_o <= c;
  len_o <= c'length;
  top_o <= c(c'left);
  low_o <= c(c'right);
  sum_o <= std_logic_vector(d);
end;
