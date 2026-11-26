-- DESCRIPTION: Verilator: VHDL slice with run-time bounds against the array direction
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- v counts down, and this slice counts up with run-time bounds.
entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal v : bit_vector(0 to 7);
  signal w : bit_vector(1 downto 0);
  signal i : integer range 1 to 7 := 1;
begin
  w <= v(i downto i - 1);
  o <= w(0);
end architecture rtl;
