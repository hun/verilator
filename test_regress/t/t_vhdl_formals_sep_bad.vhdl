-- DESCRIPTION: Verilator: VHDL subprogram interface list with no semicolon between parameters
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Interface elements in a subprogram parameter list are separated by semicolons.
-- Here the second parameter follows the first with no separator.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  procedure p (x : in bit y : in bit);
begin
  o <= '1';
end architecture rtl;
