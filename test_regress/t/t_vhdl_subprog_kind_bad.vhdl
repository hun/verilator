-- DESCRIPTION: Verilator: VHDL pure keyword before a name that is not a subprogram
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The pure keyword must be followed by function or procedure.

entity t is
  port (o : out bit);
  pure foo;
end entity t;

architecture rtl of t is
begin
  o <= '1';
end architecture rtl;
