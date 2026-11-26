-- DESCRIPTION: Verilator: VHDL record constraint on a constrained array subtype
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A record constraint names the elements of a record; this subtype is an array.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  subtype b8 is bit_vector(0 to 7);
  subtype s is b8(f(0 to 3));
begin
  o <= '0';
end architecture rtl;
