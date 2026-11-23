-- DESCRIPTION: Verilator: a configuration binding a component to a differently named entity is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (
    i : in  std_logic;
    o : out std_logic
  );
end entity child;

architecture rtl of child is
begin
  o <= i;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity other is
  port (
    i : in  std_logic;
    o : out std_logic
  );
end entity other;

architecture rtl of other is
begin
  o <= not i;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i : in  std_logic;
    o : out std_logic
  );
end entity t;

architecture rtl of t is
  component child is
    port (
      i : in  std_logic;
      o : out std_logic
    );
  end component child;
begin
  u_child : child
    port map (
      i => i,
      o => o
    );
end architecture rtl;

-- A component instance binds through the component's name, so another entity is refused
configuration cfg of t is
  for rtl
    for u_child : child
      use entity work.other;
    end for;
  end for;
end configuration cfg;
