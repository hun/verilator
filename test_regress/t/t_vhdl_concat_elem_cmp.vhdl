-- DESCRIPTION: Verilator: a concatenation of an array and an element compares as an array
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
end entity t;

architecture sim of t is
  type st is (aa, bb, cc);
  type ea is array (natural range <>) of st;
  signal s : ea(0 to 3) := (aa, bb, cc, aa);
  signal tv : ea(0 to 2) := (aa, bb, bb);
begin

  check : process
  begin
    wait for 1 ns;
    -- The concatenations are (aa, bb, bb, aa), which first differs from s at its third element
    assert (tv & aa) < s report "(tv & aa) < s should be true" severity failure;
    assert not (s < (tv & aa)) report "s < (tv & aa) should be false" severity failure;
    assert s > (tv & aa) report "s > (tv & aa) should be true" severity failure;
    assert not (s = (tv & aa)) report "s = (tv & aa) should be false" severity failure;
    assert (tv & cc) < s report "(tv & cc) < s should be true" severity failure;
    -- An element in front gives (aa, aa, bb, bb), which differs from s at its second element
    assert (aa & tv) < s report "(aa & tv) < s should be true" severity failure;
    assert not ((aa & tv) > s) report "(aa & tv) > s should be false" severity failure;
    report "all comparisons passed";
    std.env.finish;
    wait;
  end process check;
end architecture sim;
