-- DESCRIPTION: Verilator: VHDL context declaration in an entity declarative part
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A context declaration is a primary unit, not a declarative item. This parser
-- reports one in an entity declarative part as unsupported.

entity t is
  port (o : out bit);
  context cx is
    library ieee;
  end context cx;
end entity t;

architecture rtl of t is
begin
  o <= '0';
end architecture rtl;
