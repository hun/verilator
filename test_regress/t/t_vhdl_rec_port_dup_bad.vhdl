-- DESCRIPTION: Verilator: VHDL record port element diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A record port element associated more than once, with an unconstrained element.

package pk is
  type ur is record
    a : bit_vector;
    b : bit;
  end record;
end package;

use work.pk.all;

entity in_sub is
  port (p : in ur);
end entity;

architecture rtl of in_sub is
begin
end architecture;

entity top is
end entity;

architecture rtl of top is
  signal w : bit_vector(1 downto 0);
  signal y : bit;
begin
  u2 : entity work.in_sub port map (p.a => w, p.a => w, p.b => y);
end architecture;
