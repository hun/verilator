-- DESCRIPTION: Verilator: VHDL guarded block statement is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A guard on a block statement is not supported; the block is rejected at its guard.

entity t is
  port (o : out bit; en : in bit);
end entity t;

architecture rtl of t is
begin
  b: block (en = '1') is
  begin
    o <= '1';
  end block b;
end architecture rtl;
