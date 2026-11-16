-- DESCRIPTION: Verilator: an entity no instance names need not have its generics bound
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

-- Nothing instantiates this entity, so its generic never gets a value
entity unused_e is
  generic (width : natural);
  port (a : in std_logic_vector(width - 1 downto 0); y : out std_logic);
end entity unused_e;

architecture rtl of unused_e is
begin
  y <= a(0);
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
begin
  o <= "1010";
end architecture rtl;
