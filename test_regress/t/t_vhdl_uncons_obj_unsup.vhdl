-- DESCRIPTION: Verilator: VHDL object of an unconstrained array type of unconstrained elements
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A signal of an unconstrained array type has no index range to model it with.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type ia is array (natural range <>) of bit_vector;
  signal s : ia;
begin
  o <= '0';
end architecture rtl;
