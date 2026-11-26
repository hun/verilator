-- DESCRIPTION: Verilator: VHDL real value as an array index
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An index must be of a discrete type, and real is not one.
entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal v : bit_vector(0 to 3);
  signal r : real range 0.0 to 3.0 := 1.0;
begin
  o <= v(r);
end architecture rtl;
