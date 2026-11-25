-- DESCRIPTION: Verilator: VHDL interface view with a buffer element
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
    d : std_logic_vector(3 downto 0);
    q : std_logic_vector(3 downto 0);
    r : std_logic_vector(3 downto 0);
  end record;

  -- q is an output the source reads back; its converse is an input
  view src_view of rec_t is
    d : in;
    q : buffer;
    r : out;
  end view;

  alias snk_view is src_view'converse;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.pack.all;

entity src is
  port (s : view src_view);
end entity;

architecture rtl of src is
begin
  s.q <= s.d;
  s.r <= not s.q;  -- The read of a buffer element sees the value this entity drives
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.pack.all;

entity t is
  port (
    d_in  : in  std_logic_vector(3 downto 0);
    q_out : out std_logic_vector(3 downto 0);
    r_out : out std_logic_vector(3 downto 0);
    top   : view snk_view
  );
end entity;

architecture rtl of t is
  signal rec : rec_t;
begin
  u : entity work.src port map (s => rec);
  rec.d <= d_in;
  q_out <= rec.q;
  r_out <= rec.r;
  -- The converse end reads q as an input and drives d
  top.d <= not top.q;
end architecture;
