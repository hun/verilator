-- DESCRIPTION: Verilator: VHDL block header with a generic clause is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A generic clause in a block header is not supported.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  b: block is generic (w : integer := 4);
  begin
    o <= '1';
  end block b;
end architecture rtl;
