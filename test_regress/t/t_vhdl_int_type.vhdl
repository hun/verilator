-- DESCRIPTION: Verilator: VHDL an integer type definition
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity tb is
end;

architecture sim of tb is
  type small_t is range 0 to 7;
  signal s : small_t := 3;
begin
  process
  begin
    wait for 1 ns;
    assert s = 3 report "integer type value wrong" severity failure;
    std.env.finish;
    wait;
  end process;
end;
