-- DESCRIPTION: Verilator: VHDL is_x in the 2-state model
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic;
    b : in  std_logic_vector(3 downto 0);
    x : out std_logic;
    y : out std_logic
  );
end entity t;

architecture rtl of t is
begin
  -- No value is a metavalue in the 2-state model
  x <= '1' when is_x(a) else '0';
  y <= '1' when is_x(b) else '0';
end architecture rtl;
