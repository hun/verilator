-- DESCRIPTION: Verilator: VHDL entity with an alias declaration reaches the entity item error
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An alias declaration may appear in an entity declarative part, but the entity item parser has
-- no case for it, so the parser stops at the keyword.

entity t is
  port (o : out bit);
  alias al : bit is o;
end entity t;

architecture rtl of t is
begin
  o <= '1';
end architecture rtl;
