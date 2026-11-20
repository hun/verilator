-- DESCRIPTION: Verilator: VHDL a qualified architecture entity name
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity e is
  port (y : out integer);
end;

architecture rtl of e is
begin
  y <= 1;
end;

architecture alt of work.e is
begin
  y <= 2;
end;

entity tb is
end;

architecture sim of tb is
  signal y : integer := 0;
begin
  u : entity work.e(alt) port map (y => y);
  process
  begin
    wait for 1 ns;
    assert y = 2 report "qualified architecture not used" severity failure;
    std.env.finish;
    wait;
  end process;
end;
