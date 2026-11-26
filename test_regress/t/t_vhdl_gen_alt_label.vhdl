-- DESCRIPTION: Verilator: labels on the alternatives of an if generate
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is end entity;
architecture a of t is
  constant sel : integer := 3;
  signal r : integer := 0;
begin
  g1 : if sel = 2 generate
    r <= 20;
  elsif g2 : sel = 3 generate
    r <= 30;
  else g3 : generate
    r <= 40;
  end generate;
  p : process
  begin
    wait for 1 ns;
    assert r = 30 report "alternative label" severity failure;
    wait;
  end process;
end architecture;
