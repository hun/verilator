-- DESCRIPTION: Verilator: an entity that instantiates itself under a generate
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  generic (depth : natural := 3);
  port (a : in bit_vector(7 downto 0);
        y : out bit_vector(7 downto 0));
end entity t;

architecture rtl of t is
  signal shifted : bit_vector(7 downto 0);
begin
  -- Each level shifts left once, then hands the value to one level fewer
  more : if depth > 0 generate
    inner : entity work.t
      generic map (depth => depth - 1)
      port map (a => a(6 downto 0) & '0', y => y);
  end generate more;
  done : if depth = 0 generate
    y <= a;
  end generate done;
end architecture rtl;
