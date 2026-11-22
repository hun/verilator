-- DESCRIPTION: Verilator: VHDL numeric_std signed ports of several widths
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
  port (
    a    : in  signed(7 downto 0);
    b    : in  signed(7 downto 0);
    sum  : out signed(8 downto 0);
    neg  : out std_logic;
    lt   : out std_logic;
    sh   : out signed(7 downto 0);
    ab   : out signed(7 downto 0);
    ext  : out signed(15 downto 0);
    wide : out signed(39 downto 0);
    i    : out integer
  );
end;

architecture rtl of t is
begin
  sum  <= resize(a, 9) + resize(b, 9);
  neg  <= '1' when a < 0 else '0';
  lt   <= '1' when a < b else '0';
  sh   <= shift_right(a, 2);
  -- abs of the most negative value wraps to itself
  ab   <= abs(a);
  ext  <= resize(a, 16);
  wide <= resize(a, 40);
  i    <= to_integer(a);
end;
