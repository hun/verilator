-- DESCRIPTION: Verilator: VHDL interface views, ported from the NVC regression test view1
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Adapted from nvc test/regress/view1.vhd: the record compares are written per element
package pack is
  type rec_t is record
    x, y : natural;
  end record;

  view in_view of rec_t is
    x : in;
    y : out;
  end view;

  view out_view of rec_t is
    x : out;
    y : in;
  end view;
end package;

use work.pack.all;

entity sub is
  port (i : view in_view; o : view out_view);
end entity;

architecture test of sub is
begin
  o.x <= i.x + 1;
  i.y <= o.y + 5;
end architecture;

use work.pack.all;

entity t is
end entity;

architecture test of t is
  signal i, o : rec_t;
begin
  u : entity work.sub port map (i, o);

  check : process is
  begin
        assert i.x = 0 and i.y = 0
            report "i is (" & integer'image(i.x) & ", " & integer'image(i.y) & ")" severity failure;
        assert o.x = 0 and o.y = 0
            report "o is (" & integer'image(o.x) & ", " & integer'image(o.y) & ")" severity failure;
    wait for 0 ns;
        assert i.x = 0 and i.y = 5
            report "i is (" & integer'image(i.x) & ", " & integer'image(i.y) & ")" severity failure;
        assert o.x = 1 and o.y = 0
            report "o is (" & integer'image(o.x) & ", " & integer'image(o.y) & ")" severity failure;
    i.x <= 7;
    o.y <= 12;
    wait for 1 ns;
        assert i.x = 7 and i.y = 17
            report "i is (" & integer'image(i.x) & ", " & integer'image(i.y) & ")" severity failure;
        assert o.x = 8 and o.y = 12
            report "o is (" & integer'image(o.x) & ", " & integer'image(o.y) & ")" severity failure;
    std.env.finish;
  end process;
end architecture;
