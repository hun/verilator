-- DESCRIPTION: Verilator: an array indexed by an enumeration type
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o_a : out std_logic;
    o_c : out std_logic
  );
end entity t;

architecture rtl of t is
  -- An unconstrained array whose index is an enumeration takes the enum's positions
  type sel_t is (aa, bb, cc);
  type vec_t is array (sel_t) of std_ulogic;
  constant v : vec_t := ('1', '0', '1');
begin
  o_a <= v(aa);
  o_c <= v(cc);
end architecture rtl;