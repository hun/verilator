-- DESCRIPTION: Verilator: VHDL array operators need one base type
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- unsigned and std_logic_vector have the same element type but are unrelated
-- array types (IEEE 1076-2008 9.2.2, 9.2.5).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    u : in  unsigned(3 downto 0);
    v : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
begin
  y(3 downto 0) <= std_logic_vector(u + v);
  y(3 downto 0) <= u and v;
  y <= u & v;
end;
