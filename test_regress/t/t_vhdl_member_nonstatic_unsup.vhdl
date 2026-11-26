-- DESCRIPTION: Verilator: VHDL non-static bounds in a record element constraint
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The bounds of a record element constraint must be static.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal n : natural;
  type inner is record
    a : bit;
  end record;
  type outer is record
    r : inner;
    f : bit_vector(0 to 7);
  end record;
  signal x : outer(f(n to 3));
begin
  o <= '0';
end architecture rtl;
