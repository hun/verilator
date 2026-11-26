-- DESCRIPTION: Verilator: VHDL component end name not followed by a semicolon
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The end of a component declaration takes an optional name and then a semicolon.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  component c is
  end component c d;
begin
  o <= '0';
end architecture rtl;
