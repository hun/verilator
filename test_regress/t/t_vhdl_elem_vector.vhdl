-- DESCRIPTION: Verilator: an IEEE unconstrained array as an array element type
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (o : out integer);
end entity t;

architecture rtl of t is
  -- The element type is an IEEE array of integer, bounded where it is used
  type mat_t is array (0 to 1) of integer_vector(0 to 1);
  constant m : mat_t := (0 => (0 => 3, 1 => 4), 1 => (0 => 50, 1 => 60));
begin
  o <= m(0)(0) + m(1)(1);
end architecture rtl;
