-- DESCRIPTION: Verilator: a slice whose range opposes the array direction is refused
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
    i : in  unsigned(7 downto 0);
    o : out unsigned(7 downto 0)
  );
end entity t;

architecture rtl of t is
  -- Ascending over a descending index: GHDL stops elaboration with
  -- `slice direction doesn't match index direction`
  subtype asc_range_t is natural range 4 to 7;
  signal v : unsigned(7 downto 0);
begin
  v <= i;
  o <= v(asc_range_t);
end architecture rtl;
