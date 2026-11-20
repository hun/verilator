-- DESCRIPTION: Verilator: VHDL a selected architecture must exist
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity leaf is
  port (y : out integer);
end;

architecture rtl of leaf is
begin
  y <= 1;
end;

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  u : entity work.leaf(nope) port map (y => y);
end;
