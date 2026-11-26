-- DESCRIPTION: Verilator: VHDL 'range of a slice prefix
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The bounds of 'range come from the prefix type, and a slice prefix is not a name.
entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit_vector(0 to 3);
  signal v : bit_vector(0 to 3);
  signal w : bit_vector(0 to 1);
begin
  w <= v(s(0 to 1)'range);
  o <= w(0);
end architecture rtl;
