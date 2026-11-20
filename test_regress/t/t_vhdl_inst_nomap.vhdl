-- DESCRIPTION: Verilator: VHDL a component instantiation with no maps
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity leaf is
end;

architecture rtl of leaf is
begin
end;

entity tb is
end;

architecture sim of tb is
  component leaf is
  end component;
begin
  u : leaf;
  process
  begin
    wait for 1 ns;
    std.env.finish;
    wait;
  end process;
end;
