-- DESCRIPTION: Verilator: VHDL port association diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Port associations with a missing element, extra positions, default and range errors.

package pk is
  type rr is record
    a : bit;
  end record;
end package;

use work.pk.all;

entity out_sub is
  port (p : out rr);
end entity;

architecture rtl of out_sub is
begin
end architecture;

entity pos_sub is
  port (p : in bit);
end entity;

architecture rtl of pos_sub is
begin
end architecture;

entity def_sub is
  port (p : in bit_vector(3 downto 0) := "01");
end entity;

architecture rtl of def_sub is
begin
end architecture;

entity rng_sub is
  port (p : in bit_vector(3 downto 0));
end entity;

architecture rtl of rng_sub is
begin
end architecture;

entity top is
end entity;

architecture rtl of top is
  signal x : bit;
  signal y : bit;
  signal v : bit_vector(3 downto 0);
begin
  u1 : entity work.out_sub port map (p.zz => x);
  u3 : entity work.pos_sub port map (x, y);
  u4 : entity work.def_sub;
  u5 : entity work.rng_sub port map (p(7 downto 4) => v);
end architecture;
