-- DESCRIPTION: Verilator: VHDL aggregate choice list without a named association arrow
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A choice list in an aggregate must be followed by '=>' and the associated value.
-- Here the choices end the aggregate with no arrow.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal x, y : bit;
begin
  o <= (x | y);
end architecture rtl;
