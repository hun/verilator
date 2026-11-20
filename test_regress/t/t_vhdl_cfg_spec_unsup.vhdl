-- DESCRIPTION: Verilator: VHDL configuration specifications are refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The subset does not list configuration specifications, so a silent skip
-- would bind the wrong architecture.

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
  for u : a2 use entity work.a2(rtl);
begin
  u : a2 port map (y => y);
end;
