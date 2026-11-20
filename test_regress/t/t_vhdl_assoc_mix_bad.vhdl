-- DESCRIPTION: Verilator: VHDL association lists may not mix forms
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A port or generic map is either all positional or all named (IEEE 1076-2008
-- 6.5.7.2); GHDL refuses this list.

entity child is
  port (
    p1 : in  bit;
    p2 : in  bit;
    y  : out bit
  );
end;

architecture rtl of child is
begin
  y <= p1 and p2;
end;

entity t is
  port (
    a : in  bit;
    b : in  bit;
    y : out bit
  );
end;

architecture rtl of t is
begin
  u1 : entity work.child port map (a, p2 => b, y => open);
end;
