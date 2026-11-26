-- DESCRIPTION: Verilator: VHDL entity end designator that differs from the entity name
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The designator after end entity must repeat the entity name.

entity t is
  port (o : out bit);
end entity u;

architecture rtl of t is
begin
  o <= '1';
end architecture rtl;
