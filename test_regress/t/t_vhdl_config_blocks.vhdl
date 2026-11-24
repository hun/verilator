-- DESCRIPTION: Verilator: block configurations nested in a configuration
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (i : in std_logic;
        o : out std_logic);
end entity child;

architecture rtl of child is
begin
  o <= i;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity other is
  port (i : in std_logic;
        o : out std_logic);
end entity other;

architecture rtl of other is
begin
  o <= not i;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (i : in std_logic;
        outer : out std_logic;
        inner : out std_logic);
end entity t;

architecture rtl of t is
  component child is
    port (i : in std_logic;
          o : out std_logic);
  end component child;
begin
  u_outer : child port map (i => i, o => outer);

  gen : for k in 0 to 0 generate
    u_inner : child port map (i => i, o => inner);
  end generate gen;
end architecture rtl;

-- The generate is configured by a block of its own, and the instance in it by a component
-- configuration; the bound entity of another name replaces the component
configuration t_conf of t is
  for rtl
    for gen
      for u_inner : child
        use entity work.other;
      end for;
    end for;
  end for;
end configuration t_conf;
