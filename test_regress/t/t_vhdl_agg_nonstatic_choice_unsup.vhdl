-- DESCRIPTION: Verilator: VHDL aggregate choice that is not a locally static value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal k : integer range 0 to 3;
  signal v : bit_vector(0 to 3);
begin
  v <= (k => '1', others => '0');
  o <= v(0);
end architecture rtl;
