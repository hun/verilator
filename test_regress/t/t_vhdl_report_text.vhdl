-- DESCRIPTION: Verilator: a report message built from literals and integer images
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is end entity;

architecture a of t is
begin
  process
    variable n : integer := 42;
    variable s : natural := 7;
  begin
    report "plain 100% done";
    report "count " & integer'image(n) & " and " & integer'image(s + 1) & "!";
    report "char " & 'x' & " paren " & (integer'image(-5));
    assert n = 41 report "n was " & integer'image(n) severity note;
    std.env.finish;
    wait;
  end process;
end architecture;
