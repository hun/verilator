-- DESCRIPTION: Verilator: VHDL interface view ports with converse and view-of forms
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package pack is
  type rec_t is record
    x, y : std_logic_vector(7 downto 0);
  end record;

  view in_view of rec_t is
    x : in;
    y : out;
  end view;

  alias out_view is in_view'converse;

  -- Written out instead of derived with 'converse
  view out_direct of rec_t is
    x : out;
    y : in;
  end view;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pack.all;

-- The view named by an alias of its converse
entity sub is
  port (i : view in_view; o : view out_view);
end;

architecture rtl of sub is
begin
  o.x <= std_logic_vector(unsigned(i.x) + 1);
  i.y <= std_logic_vector(unsigned(o.y) + 5);
end;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pack.all;

-- The view with an explicit record type and the converse written on the port
entity sub_of is
  port (i : view in_view of rec_t; o : view in_view'converse of rec_t);
end;

architecture rtl of sub_of is
begin
  o.x <= std_logic_vector(unsigned(i.x) + 1);
  i.y <= std_logic_vector(unsigned(o.y) + 5);
end;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pack.all;

entity sub_direct is
  port (i : view in_view; o : view out_direct);
end;

architecture rtl of sub_direct is
begin
  o.x <= std_logic_vector(unsigned(i.x) + 1);
  i.y <= std_logic_vector(unsigned(o.y) + 5);
end;

library ieee;
use ieee.std_logic_1164.all;
use work.pack.all;

entity t is
  port (
    xin   : in  std_logic_vector(7 downto 0);
    yin   : in  std_logic_vector(7 downto 0);
    xout  : out std_logic_vector(7 downto 0);
    yout  : out std_logic_vector(7 downto 0);
    xout2 : out std_logic_vector(7 downto 0);
    yout2 : out std_logic_vector(7 downto 0);
    xout3 : out std_logic_vector(7 downto 0);
    yout3 : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
  signal i, o, i2, o2, i3, o3 : rec_t;
begin
  u1 : entity work.sub port map (i, o);
  u2 : entity work.sub_of port map (o => o2, i => i2);
  u3 : entity work.sub_direct port map (i3, o3);

  i.x <= xin;
  o.y <= yin;
  xout <= o.x;
  yout <= i.y;

  i2.x <= xin;
  o2.y <= yin;
  xout2 <= o2.x;
  yout2 <= i2.y;

  i3.x <= xin;
  o3.y <= yin;
  xout3 <= o3.x;
  yout3 <= i3.y;
end;
