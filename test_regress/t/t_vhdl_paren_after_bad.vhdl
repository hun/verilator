-- DESCRIPTION: Verilator: VHDL port map not followed by a parenthesized association list
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The port map keyword must be followed by a parenthesized association list.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  u1 : sub port map o => o;
end architecture rtl;
