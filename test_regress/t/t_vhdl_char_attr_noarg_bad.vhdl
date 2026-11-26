-- DESCRIPTION: Verilator: VHDL character position attribute without an argument is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- 'pos takes one argument (IEEE 1076-2008 16.2.2). The lowering reports the
-- missing argument as unsupported.

entity t is
  port (o : out integer);
end entity t;

architecture rtl of t is
begin
  o <= character'pos;
end architecture rtl;
