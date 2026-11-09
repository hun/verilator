-- DESCRIPTION: Verilator: VHDL individual record port associations
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    b : in std_logic_vector(3 downto 0);
    c : in std_logic_vector(3 downto 0);
    d : in std_logic_vector(3 downto 0);
    ux : out std_logic_vector(3 downto 0);
    uy : out std_logic_vector(3 downto 0);
    cx : out std_logic_vector(3 downto 0);
    cy : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- Unconstrained record ports specialized from the individual element actuals
  u : entity work.child_u
    port map (
      i.x => a,
      i.y => b,
      o.x => ux,
      o.y => uy
    );

  -- Constrained record ports with the same element-wise association form, including
  -- an expression actual
  v : entity work.child_c
    port map (
      j.x => b,
      j.y => c xor d,
      o.x => cx,
      o.y => cy
    );

  -- The child's output port `a` is unassociated and shares its name with a
  -- record member, so it must still resolve to an artificial open of `a`
  w : entity work.child_o
    port map (
      i.a => a,
      i.b => b
    );
end;
