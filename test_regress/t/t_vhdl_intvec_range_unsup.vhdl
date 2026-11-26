-- DESCRIPTION: Verilator: VHDL integer_vector element of an array type without a static range
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An unconstrained integer_vector element with no range to take its bounds from.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type ia is array (natural range <>) of integer_vector;
begin
  o <= '0';
end architecture rtl;
