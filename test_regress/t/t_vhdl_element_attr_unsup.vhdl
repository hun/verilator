-- DESCRIPTION: Verilator: VHDL element attribute of a scalar object
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The element attribute applies to an array, and bit is a scalar.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal x : bit;
  signal y : x'element;
begin
  o <= '0';
end architecture rtl;
