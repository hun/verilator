-- DESCRIPTION: Verilator: VHDL unconstrained ports on an entity with generics
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity sub is
  generic (g_width : integer := 4; g_inv : boolean := false);
  port (
    i_a   : in  std_logic_vector;
    o_y   : out std_logic_vector;
    o_len : out integer range 0 to 15;
    o_gen : out integer range 0 to 15
    );
end entity;

architecture rtl of sub is
begin
  -- The port ranges come from the instance's actuals, the values from its generics
  o_y <= i_a when g_inv else not i_a;
  o_len <= i_a'length;
  o_gen <= g_width;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a     : in  std_logic_vector(7 downto 0);
    y1    : out std_logic_vector(3 downto 0);
    y2    : out std_logic_vector(7 downto 0);
    len1  : out integer range 0 to 15;
    len2  : out integer range 0 to 15;
    gen1  : out integer range 0 to 15;
    gen2  : out integer range 0 to 15
    );
end entity;

architecture rtl of t is
begin
  -- Two instances of one entity that binds generics and constrains the same
  -- unconstrained ports with differently sized actuals
  u1 : entity work.sub
    generic map (g_width => 4, g_inv => false)
    port map (
      i_a   => a(3 downto 0),
      o_y   => y1,
      o_len => len1,
      o_gen => gen1
      );

  u2 : entity work.sub
    generic map (g_width => 2, g_inv => true)
    port map (
      i_a   => a,
      o_y   => y2,
      o_len => len2,
      o_gen => gen2
      );
end architecture;
