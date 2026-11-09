-- DESCRIPTION: Verilator: VHDL array with multi-bit elements
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    i : in integer;
    o : out std_logic_vector(31 downto 0)
  );
end;

architecture rtl of t is
  type mem_t is array (0 to 3) of std_logic_vector(31 downto 0);
  signal m : mem_t;
begin
  o <= m(i);
end;
