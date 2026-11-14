-- DESCRIPTION: Verilator: a configuration that binds an entity the architecture instantiates
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
begin
  u_child : entity work.child
    port map (
      i => i,
      o => o
    );
end architecture rtl;

-- The configuration binds the instance to the entity the architecture already instantiates,
-- so it changes nothing and the design elaborates
configuration cfg of t is
  for rtl
    for u_child : child
      use entity work.child;
    end for;
  end for;
end configuration cfg;