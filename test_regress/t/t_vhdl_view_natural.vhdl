-- DESCRIPTION: Verilator: VHDL interface view of a record with natural elements
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package pack is
  type rec_t is record
    x, y : natural;
  end record;

  view in_view of rec_t is
    x : in;
    y : out;
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
end architecture;

use work.pack.all;

entity t is
  port (
    xin  : in  natural;
    yin  : in  natural;
    xout : out natural;
    yout : out natural
  );
end entity;

architecture test of t is
  signal i, o : rec_t;
begin
  u : entity work.sub port map (i, o);
  i.x <= xin;
  o.y <= yin;
  xout <= o.x;
  yout <= i.y;
end architecture;
