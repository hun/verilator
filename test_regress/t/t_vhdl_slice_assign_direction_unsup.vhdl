-- DESCRIPTION: Verilator: slice assignment against the array direction
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The slice direction differs from the array direction and its bounds are run-time values.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit_vector(0 to 7);
  signal i, j : natural := 3;
begin
  s(i downto j) <= "101";
  o <= s(0);
end architecture rtl;
