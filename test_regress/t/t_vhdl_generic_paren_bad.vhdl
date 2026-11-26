-- DESCRIPTION: Verilator: VHDL generic clause without an opening parenthesis
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
--
-- The generic keyword must be followed by a parenthesized interface list. Here the list has no opening parenthesis.

entity t is
  generic n : integer := 1;
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;
