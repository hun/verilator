-- DESCRIPTION: Verilator: VHDL non-static range constraint on an array subtype
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The bounds of a range constraint must be static.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal n : natural;
  signal s : bit_vector(0 to n);
begin
  o <= '0';
end architecture rtl;
