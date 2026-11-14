-- DESCRIPTION: Verilator: a configuration that binds a component instantiation is refused
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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

-- A component instantiation binds to a component declaration, not to an entity, so the
-- frontend would have to resolve the component to lower it
configuration cfg of t is
  for rtl
    for u_child : child
      use entity work.child;
    end for;
  end for;
end configuration cfg;