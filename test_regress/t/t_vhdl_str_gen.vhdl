-- DESCRIPTION: Verilator: VHDL string-generic generate condition
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity seldrv is
  generic (val : string);
  port (o : out std_logic);
end entity;

architecture rtl of seldrv is
begin
  g1 : if val = "one" generate
    o <= '1';
  end generate;
  g0 : if val /= "one" generate
    o <= '0';
  end generate;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o1 : out std_logic;
        o2 : out std_logic);
end entity;

architecture rtl of t is
begin
  i1 : entity work.seldrv
    generic map (val => "one")
    port map (o => o1);
  i2 : entity work.seldrv
    generic map (val => "two")
    port map (o => o2);
end architecture;
