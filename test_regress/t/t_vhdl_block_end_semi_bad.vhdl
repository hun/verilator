-- DESCRIPTION: Verilator: VHDL block statement end with no semicolon
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A block statement ends with ';' after its optional end label.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  b: block is
  begin
    o <= '1';
  end block b
end architecture rtl;
