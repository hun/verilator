-- DESCRIPTION: Verilator: VHDL configuration declaration binding with an architecture selection is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A configuration declaration is only checked, so a binding that selects an architecture
-- is refused rather than silently ignored.

entity a2 is
  port (y : out integer);
end;

architecture rtl of a2 is
begin
  y <= 1;
end;

entity t is
  port (y : out integer);
end;

architecture rtl of t is
  component a2 is
    port (y : out integer);
  end component;
begin
  u : a2 port map (y => y);
end;

configuration cfg of t is
  for rtl
    for u : a2
      use entity work.a2(rtl);
    end for;
  end for;
end configuration;
