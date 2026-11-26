-- DESCRIPTION: Verilator: attribute of a qualified expression prefix
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The prefix of the attribute is not a name.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal s : bit_vector(0 to 3);
begin
  o <= '1' when bit_vector'(s)'length = 4 else '0';
end architecture rtl;
