-- DESCRIPTION: Verilator: VHDL generic whose range or default names an earlier generic
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity sub is
  generic (depth : positive;  -- No default: only an instance can elaborate this entity
           almost_full_level : natural range 0 to depth := depth);
  port (
    i_a      : in  std_logic;
    o_level  : out natural range 0 to depth;
    o_almost : out natural range 0 to depth
    );
end entity;

architecture rtl of sub is
begin
  o_level <= depth when i_a else 0;
  o_almost <= almost_full_level when i_a else 0;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i_a  : in  std_logic;
    o_l1 : out natural range 0 to 8;
    o_a1 : out natural range 0 to 8;
    o_l2 : out natural range 0 to 4;
    o_a2 : out natural range 0 to 4
    );
end entity;

architecture rtl of t is
begin
  -- u1 leaves almost_full_level at its default, which names the generic bound above it
  u1 : entity work.sub
    generic map (depth => 8)
    port map (
      i_a      => i_a,
      o_level  => o_l1,
      o_almost => o_a1
      );

  u2 : entity work.sub
    generic map (depth => 4, almost_full_level => 2)
    port map (
      i_a      => i_a,
      o_level  => o_l2,
      o_almost => o_a2
      );
end architecture;
