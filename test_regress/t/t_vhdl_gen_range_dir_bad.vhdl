-- DESCRIPTION: Verilator: VHDL for-generate range with a misspelt direction keyword
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A generate range needs 'to' or 'downto' between its bounds (IEEE 1076-2008 5.2.1).
-- The misspelt keyword 'dwonto' is not a direction, so the parser rejects it.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  constant n : integer := 1;
begin
  g: for i in n dwonto 0 generate
    o <= '1';
  end generate g;
end architecture rtl;
