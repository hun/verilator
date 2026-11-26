-- DESCRIPTION: Verilator: a conditional concurrent assignment with two different delays is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Only one delay per conditional assignment is accepted.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal a : bit;
begin
  o <= '1' after 1 ns when a = '1' else '0' after 2 ns;
end architecture rtl;
