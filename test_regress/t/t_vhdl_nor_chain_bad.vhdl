-- DESCRIPTION: Verilator: VHDL non-associative logical operators need parentheses
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The operators nand and nor are non-associative, and operators of one
-- precedence level may not be mixed without parentheses (IEEE 1076-2008 9.1).
-- GHDL refuses every expression below.

entity t is
  port (
    a : in  bit;
    b : in  bit;
    c : in  bit;
    y : out bit
  );
end;

architecture rtl of t is
begin
  y <= a nor b nor c;
end;
