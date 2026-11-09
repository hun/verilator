-- DESCRIPTION: Verilator: VHDL std_logic_arith conversion builtins
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
-- Deliberately only std_logic_arith: mixing it with numeric_std is ambiguous
use ieee.std_logic_arith.all;

entity t is
  port (
    u : out std_logic_vector(7 downto 0);
    s : out std_logic_vector(7 downto 0);
    w : out unsigned(7 downto 0);
    x : out signed(7 downto 0)
  );
end;

architecture rtl of t is
begin
  u <= conv_std_logic_vector(5, 8);
  s <= conv_std_logic_vector(-1, 8);
  w <= conv_unsigned(5, 8);
  x <= conv_signed(-1, 8);
end;
