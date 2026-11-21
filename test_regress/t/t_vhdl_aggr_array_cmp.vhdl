-- DESCRIPTION: Verilator: an aggregate compared with a constrained array takes that array's type
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
end entity t;

architecture sim of t is
  type ia is array (0 to 1) of integer;
  signal s : ia := (3, 4);
begin

  check : process
  begin
    wait for 1 ns;
    -- The aggregate has the index range of s, so the ordering is element by element
    assert not (s < (1, 2)) report "s < (1, 2) should be false" severity failure;
    assert s > (1, 2) report "s > (1, 2) should be true" severity failure;
    assert s < (3, 5) report "s < (3, 5) should be true" severity failure;
    assert not (s < (3, 4)) report "s < (3, 4) should be false" severity failure;
    assert s <= (3, 4) report "s <= (3, 4) should be true" severity failure;
    assert s >= (3, 4) report "s >= (3, 4) should be true" severity failure;
    assert s = (3, 4) report "s = (3, 4) should be true" severity failure;
    assert s /= (1, 2) report "s /= (1, 2) should be true" severity failure;
    assert (1, 2) < s report "(1, 2) < s should be true" severity failure;
    assert not ((4, 0) < s) report "(4, 0) < s should be false" severity failure;
    report "all comparisons passed";
    std.env.finish;
    wait;
  end process check;
end architecture sim;
