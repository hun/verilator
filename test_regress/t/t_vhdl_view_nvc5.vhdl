-- DESCRIPTION: Verilator: VHDL array of interface views, ported from the NVC regression test view5
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Adapted from nvc test/regress/view5.vhd: the record compares are written per element
package pack is
  type rec_t is record
    x, y : natural;
  end record;

  type rec_array_t is array (natural range <>) of rec_t;

  view in_view of rec_t is
    x : in;
    y : out;
  end view;

  alias out_view is in_view'converse;
end package;

use work.pack.all;

entity sub is
  port (i : view (in_view) of rec_array_t(0 to 0);
        o : view (out_view) of rec_array_t(0 to 0));
end entity;

architecture test of sub is
begin
  o(0).x <= i(0).x + 1;
  i(0).y <= o(0).y + 5;
end architecture;

use work.pack.all;

entity t is
end entity;

architecture test of t is
  signal i, o : rec_array_t(0 to 0);
begin
  u : entity work.sub port map (i, o);

  check : process is
  begin
        assert i(0).x = 0 and i(0).y = 0
            report "i(0) is wrong" severity failure;
        assert o(0).x = 0 and o(0).y = 0
            report "o(0) is wrong" severity failure;
    wait for 0 ns;
        assert i(0).x = 0 and i(0).y = 5
            report "i(0) is wrong" severity failure;
        assert o(0).x = 1 and o(0).y = 0
            report "o(0) is wrong" severity failure;
    i(0).x <= 7;
    o(0).y <= 12;
    wait for 1 ns;
        assert i(0).x = 7 and i(0).y = 17
            report "i(0) is wrong" severity failure;
        assert o(0).x = 8 and o(0).y = 12
            report "o(0) is wrong" severity failure;
    std.env.finish;
  end process;
end architecture;
