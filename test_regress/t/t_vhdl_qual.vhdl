-- DESCRIPTION: Verilator: VHDL qualified aggregates
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
    b : in std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0);
    z : out std_logic_vector(3 downto 0);
    w : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- A one-element qualification, whose bounds come from the choices
  y <= std_logic_vector(unsigned(a) + unsigned'(0 => b(0)));
  -- The leftmost index is the most significant element of an aggregate
  z <= std_logic_vector(unsigned'(1 => a(1), 0 => a(0)));
  -- A range choice fixes the length
  w <= std_logic_vector(unsigned'(1 to 2 => '1'));
end;
