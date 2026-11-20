-- DESCRIPTION: Verilator: VHDL subtype mark used as a slice range
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
    width : positive := 8
  );
  port (
    i_addr  : in  unsigned(width - 1 downto 0);
    o_low   : out unsigned(3 downto 0);
    o_high  : out unsigned(3 downto 0);
    o_write : out unsigned(3 downto 0)
  );
end entity t;

architecture rtl of t is
  -- A subtype mark is a discrete range, so it selects the slice its bounds denote; the
  -- bounds may name a generic. Its direction must match the array's, as for any slice.
  subtype low_range_t is natural range 3 downto 0;
  subtype high_range_t is natural range width - 1 downto 4;
  signal packed : unsigned(width - 1 downto 0) := (others => '0');
begin
  o_low  <= i_addr(low_range_t);
  o_high <= i_addr(high_range_t);
  -- The same form as an assignment target writes only the selected elements
  packed(low_range_t) <= "0101";
  o_write             <= packed(low_range_t);
end architecture rtl;
