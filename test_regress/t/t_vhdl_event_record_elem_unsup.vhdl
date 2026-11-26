-- DESCRIPTION: Verilator: 'event on a record element prefix
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The 'event attribute is lowered only for a plain signal name here.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type r is record
    f : bit;
  end record;
  signal rr : r;
begin
  o <= '1' when rr.f'event else '0';
end architecture rtl;
