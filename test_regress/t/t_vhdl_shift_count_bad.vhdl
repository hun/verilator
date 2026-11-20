-- DESCRIPTION: Verilator: VHDL shift counts are integers
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    v : in  std_logic_vector(3 downto 0);
    b : in  boolean;
    y : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  y <= v sll '1';
  y <= v srl b;
end;
