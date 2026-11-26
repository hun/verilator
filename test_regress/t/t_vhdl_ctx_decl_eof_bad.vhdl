-- DESCRIPTION: Verilator: VHDL context declaration that runs to the end of the file
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The context declaration below has no end context clause before the end of the file.
-- The parser reports this as unsupported rather than as a syntax error.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  o <= '0';
end architecture rtl;

context cx is
