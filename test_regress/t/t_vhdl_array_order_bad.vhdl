-- DESCRIPTION: Verilator: VHDL ordering of an array of vectors is rejected
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out bit);
end;

architecture rtl of t is
  type vecarr_t is array (natural range <>) of std_logic_vector(1 downto 0);
  constant va : vecarr_t(0 to 1) := ("01", "10");
  constant vb : vecarr_t(0 to 1) := ("01", "11");
begin
  -- Predefined ordering requires a discrete element type
  o <= '1' when va < vb else '0';
end;
