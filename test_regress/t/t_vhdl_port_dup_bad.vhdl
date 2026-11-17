-- DESCRIPTION: Verilator: a port associated twice in one instance
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (a : in std_logic; y : out std_logic);
end entity;

architecture rtl of leaf is
begin
  y <= a;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t_dup is
  port (x : in std_logic; z : in std_logic; o : out std_logic);
end entity;

architecture rtl of t_dup is
begin
  u : entity work.leaf port map (a => x, a => z, y => o);
end architecture;
