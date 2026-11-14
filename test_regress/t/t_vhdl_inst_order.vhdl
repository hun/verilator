-- DESCRIPTION: Verilator: entity instantiated before its declaration is elaborated
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    x   : in  std_ulogic_vector(1 downto 0);
    y   : in  std_ulogic_vector(1 downto 0);
    sum : out std_ulogic_vector(1 downto 0);
    car : out std_ulogic_vector(1 downto 0);
    s0  : out std_ulogic;
    c0  : out std_ulogic
  );
end entity t;

architecture rtl of t is
  -- cell is declared below this architecture; a component declaration is how its users
  -- name it
  component cell
    port (
      x   : in  std_ulogic;
      y   : in  std_ulogic;
      sum : out std_ulogic;
      car : out std_ulogic
    );
  end component cell;
begin
  -- CHECK values: reached through a generate body, so the child's ports have to be
  -- elaborated before these associations are bound
  gen : for i in 0 to 1 generate
    cell_inst : cell
      port map (x => x(i), y => y(i), sum => sum(i), car => car(i));
  end generate gen;

  -- CHECK values: the same entity bound by the component name
  plain : cell port map (x => '1', y => '0', sum => s0, car => c0);
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity cell is
  port (
    x   : in  std_ulogic;
    y   : in  std_ulogic;
    sum : out std_ulogic;
    car : out std_ulogic
  );
end entity cell;

library ieee;
use ieee.std_logic_1164.all;

architecture rtl of cell is
begin
  sum <= x xor y;
  car <= x and y;
end architecture rtl;
