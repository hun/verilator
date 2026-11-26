-- DESCRIPTION: Verilator: VHDL subprogram designator that is not a name or string
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A function specification needs a name or an operator string after the keyword.
-- A parenthesis there is a syntax error.

entity t is
  port (o : out bit);
  function (x : bit) return bit;
end entity t;

architecture rtl of t is
begin
  o <= '0';
end architecture rtl;
