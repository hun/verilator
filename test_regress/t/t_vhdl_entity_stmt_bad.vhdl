-- DESCRIPTION: Verilator: VHDL non-passive entity statement diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An entity statement part holds only passive statements (IEEE 1076-2008 3.2.4).

entity child is
end entity;

architecture rtl of child is
begin
end architecture;

entity top is
begin
  u : entity work.child;
end entity;

architecture rtl of top is
begin
end architecture;
