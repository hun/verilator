-- DESCRIPTION: Verilator: VHDL 'image of the character type is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- 'image of the predefined character type is not lowered.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal c : character;
  signal s : string(1 to 3);
begin
  s <= character'image(c);
  o <= '1';
end architecture rtl;
