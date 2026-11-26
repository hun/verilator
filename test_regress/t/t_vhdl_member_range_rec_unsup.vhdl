-- DESCRIPTION: Verilator: VHDL range constraint on a record element
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Element r is a record, so its constraint must be a record constraint.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type inner is record
    a : bit;
  end record;
  type outer is record
    r : inner;
    f : bit_vector(0 to 7);
  end record;
  subtype s is outer(r(0 to 3));
begin
  o <= '0';
end architecture rtl;
