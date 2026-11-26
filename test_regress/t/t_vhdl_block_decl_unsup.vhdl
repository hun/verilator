-- DESCRIPTION: Verilator: VHDL block configuration specification is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A configuration specification in a block declarative part is not supported.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  component c is
    port (x : out bit);
  end component c;
begin
  b: block is
    for all : c use entity work.c;
  begin
    o <= '1';
  end block b;
end architecture rtl;
