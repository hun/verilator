-- DESCRIPTION: Verilator: VHDL comma index past a bit vector element
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A bit vector has one index, so the second index in v(0, 1) has no array to index.
entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit_vector(0 to 3);
  signal v : bit_vector(0 to 3);
  signal w : bit_vector(0 to 1);
begin
  o <= v(0, 1);
end architecture rtl;
