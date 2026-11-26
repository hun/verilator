-- DESCRIPTION: Verilator: a record aggregate of only others
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (a    : in natural range 0 to 15;
        b    : in natural range 0 to 15;
        zero : out bit;
        sum  : out natural);
end entity t;

architecture rtl of t is
  type rec_t is record
    f : natural range 0 to 15;
    g : natural range 0 to 15;
  end record;

  -- Both elements take the default's one value
  function total (constant v : in rec_t := (others => 3)) return natural is
  begin
    return v.f + v.g;
  end function total;

  signal r : rec_t;
begin
  r.f <= a;
  r.g <= b;
  zero <= '1' when r = (others => 0) else '0';
  sum  <= total;
end architecture rtl;
