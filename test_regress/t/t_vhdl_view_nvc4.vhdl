-- DESCRIPTION: Verilator: VHDL nested interface views, ported from the NVC regression test view4
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Adapted from nvc test/regress/view4.vhd: the resolved inout element becomes an out element,
-- and the record compares are written per element
package pack is
  type vec_t is record
    p : bit_vector(1 to 3);
    q : bit_vector(1 to 3);
  end record;

  view vec_in_view of vec_t is
    p : in;
    q : out;
  end view;

  type rec_t is record
    x, y : natural;
    z : vec_t;
  end record;

  view in_view of rec_t is
    x : in;
    y : out;
    z : view vec_in_view;
  end view;

  alias out_view is in_view'converse;
end package;

use work.pack.all;

entity sub is
  port (i : view in_view; o : view out_view);
end entity;

architecture test of sub is
begin
  o.x <= i.x + 1;
  i.y <= o.y + 5;
  o.z.p <= not i.z.p;
  i.z.q <= not o.z.q;
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
        assert i.x = 0 and i.y = 0 and i.z.p = "000" and i.z.q = "000"
            report "i is wrong" severity failure;
        assert o.x = 0 and o.y = 0 and o.z.p = "000" and o.z.q = "000"
            report "o is wrong" severity failure;
    wait for 0 ns;
        assert i.x = 0 and i.y = 5 and i.z.p = "000" and i.z.q = "111"
            report "i is wrong" severity failure;
        assert o.x = 1 and o.y = 0 and o.z.p = "111" and o.z.q = "000"
            report "o is wrong" severity failure;
    i.x <= 7;
    o.y <= 12;
    i.z.p <= "101";
    o.z.q <= "001";
    wait for 1 ns;
        assert i.x = 7 and i.y = 17 and i.z.p = "101" and i.z.q = "110"
            report "i is wrong" severity failure;
        assert o.x = 8 and o.y = 12 and o.z.p = "010" and o.z.q = "001"
            report "o is wrong" severity failure;
    std.env.finish;
  end process;
end architecture;
