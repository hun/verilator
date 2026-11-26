-- DESCRIPTION: Verilator: a generate statement whose end generate lacks a semicolon is a parse error
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The end generate needs a semicolon.

entity t is
  port (o : out bit_vector(0 to 1));
end entity t;

architecture rtl of t is
  signal a : bit;
begin
  g : for i in 0 to 1 generate
    o(i) <= a;
  end generate g
end architecture rtl;
