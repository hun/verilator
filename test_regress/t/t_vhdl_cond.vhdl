-- DESCRIPTION: Verilator: VHDL conditional and selected assignments
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    sel : in std_logic;
    a   : in std_logic;
    b   : in std_logic;
    y   : out std_logic;
    z   : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
begin
  y <= a when sel = '1' else b;
  with sel select z <= "00" when '0', "11" when others;
end;
