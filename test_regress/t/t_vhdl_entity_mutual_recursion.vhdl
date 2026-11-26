-- DESCRIPTION: Verilator: two entities that instantiate each other under a generate
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity u is
  generic (depth : natural := 0);
  port (a : in bit_vector(3 downto 0);
        y : out bit_vector(3 downto 0));
end entity u;

entity t is
  generic (depth : natural := 3);
  port (a : in bit_vector(3 downto 0);
        y : out bit_vector(3 downto 0));
end entity t;

-- t shifts left, u shifts right filling with one, and each hands on to the other
architecture rtl of t is
begin
  more : if depth > 0 generate
    inner : entity work.u
      generic map (depth => depth - 1)
      port map (a => a(2 downto 0) & '0', y => y);
  end generate more;
  done : if depth = 0 generate
    y <= a;
  end generate done;
end architecture rtl;

architecture rtl of u is
begin
  more : if depth > 0 generate
    inner : entity work.t
      generic map (depth => depth - 1)
      port map (a => '1' & a(3 downto 1), y => y);
  end generate more;
  done : if depth = 0 generate
    y <= a;
  end generate done;
end architecture rtl;
