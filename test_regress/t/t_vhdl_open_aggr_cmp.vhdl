-- DESCRIPTION: Verilator: an aggregate compared with an array of an unconstrained type takes its own bounds
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
end entity t;

architecture sim of t is
  type ia is array (natural range <>) of integer;
  signal s : ia(0 to 1) := (3, 4);
begin

  check : process
  begin
    wait for 1 ns;
    -- The aggregate's index range is its own, so it is one element long here
    assert s < (0 => 5) report "s < (0 => 5) should be true" severity failure;
    assert not (s < (0 => 3)) report "s < (0 => 3) should be false" severity failure;
    assert s > (0 => 3) report "s > (0 => 3) should be true" severity failure;
    assert s >= (0 => 3) report "s >= (0 => 3) should be true" severity failure;
    assert not (s = (0 => 3)) report "s = (0 => 3) should be false" severity failure;
    assert s /= (0 => 3) report "s /= (0 => 3) should be true" severity failure;
    assert (0 => 2) < s report "(0 => 2) < s should be true" severity failure;
    assert not ((0 => 5) < s) report "(0 => 5) < s should be false" severity failure;
    -- A longer aggregate is also taken as it is written, and a shorter prefix is less
    assert s < (3, 4, 0) report "s < (3, 4, 0) should be true" severity failure;
    assert not (s < (3, 4)) report "s < (3, 4) should be false" severity failure;
    assert s <= (3, 4) report "s <= (3, 4) should be true" severity failure;
    assert not (s < (3, 0)) report "s < (3, 0) should be false" severity failure;
    report "all comparisons passed";
    std.env.finish;
    wait;
  end process check;
end architecture sim;
