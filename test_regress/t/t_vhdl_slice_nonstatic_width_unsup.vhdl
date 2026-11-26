-- DESCRIPTION: Verilator: VHDL slice with a run-time width
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A slice whose position is dynamic must still have a static width.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit_vector(0 to 7);
  signal i, j : natural;
  signal v : bit_vector(0 to 1);
begin
  v <= s(i to j);
  o <= v(0);
end architecture rtl;
