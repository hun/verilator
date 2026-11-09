-- DESCRIPTION: Verilator: VHDL individual slice port associations
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (
    a : in std_logic_vector(7 downto 0);
    b : in std_logic_vector;
    y : out std_logic_vector(7 downto 0);
    z : out std_logic_vector
  );
end;

architecture rtl of child is
begin
  y <= a xor b;
  z <= a and b;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i0 : in std_logic_vector(3 downto 0);
    i1 : in std_logic_vector(3 downto 0);
    i2 : in std_logic_vector(3 downto 0);
    i3 : in std_logic_vector(3 downto 0);
    o0 : out std_logic_vector(3 downto 0);
    o1 : out std_logic_vector(3 downto 0);
    o2 : out std_logic_vector(3 downto 0);
    o3 : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- Constrained ports sliced in the association, and an unconstrained port
  -- specialized from the associated slices
  u : entity work.child
    port map (
      a(7 downto 4) => i0,
      a(3 downto 0) => i1,
      b(0 to 3) => i2,
      b(4 to 7) => i3,
      y(7 downto 4) => o0,
      y(3 downto 0) => o1,
      z(0 to 3) => o2,
      z(4 to 7) => o3
    );
end;
