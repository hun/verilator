-- DESCRIPTION: Verilator: VHDL numeric_std conversion builtins
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    y : out std_logic_vector(7 downto 0);
    z : out std_logic_vector(3 downto 0);
    v : out std_logic_vector(7 downto 0);
    w : out integer
  );
end;

architecture rtl of t is
  signal u : unsigned(3 downto 0);
begin
  u <= unsigned(a);
  -- resize of an unsigned value zero-extends
  y <= std_logic_vector(resize(u, 8));
  -- static to_signed folds to an aggregate with a literal origin
  z <= std_logic_vector(resize(to_signed(-1, 4), 4));
  -- resize of a signed value sign-extends
  v <= std_logic_vector(resize(to_signed(-1, 4), 8));
  w <= to_integer(u);
end;
