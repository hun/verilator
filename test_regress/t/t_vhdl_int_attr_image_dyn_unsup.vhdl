-- DESCRIPTION: Verilator: VHDL 'image of integer with a non-constant argument is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- 'image of integer with a signal argument is not lowered;
-- only a static argument is folded.

entity t is
  port (i : in integer; o : out bit);
end entity t;

architecture rtl of t is
  signal str : string(1 to 2);
begin
  str <= integer'image(i);
  o <= '1';
end architecture rtl;
