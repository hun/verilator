-- DESCRIPTION: Verilator: VHDL subtype declaration without a semicolon
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A subtype declaration ends with ';' (IEEE 1076-2008 6.3).

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  subtype sm is integer range 0 to 3
begin
  o <= '0';
end architecture rtl;
