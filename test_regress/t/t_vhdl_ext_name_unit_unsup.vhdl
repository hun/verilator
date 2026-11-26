-- DESCRIPTION: Verilator: an external name that leaves the unit has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The ^ prefix climbs above the architecture, which this unit does not model.


entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit;
begin
  p : process
    variable v : bit;
  begin
    v := <<signal ^.s : bit>>;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
