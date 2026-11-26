-- DESCRIPTION: Verilator: VHDL array type whose index mark has no range
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An index that is a type mark takes its range; this mark gives none.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type ta is array (bit) of bit;
begin
  o <= '0';
end architecture rtl;
