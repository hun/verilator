-- DESCRIPTION: Verilator: VHDL file declaration inside a function without its semicolon
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A file declaration in a subprogram declarative part ends with a semicolon, here it is missing.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  function f (a : bit) return bit is
    file x : text
  begin
    return a;
  end function f;
begin
  o <= f('1');
end architecture rtl;
