-- DESCRIPTION: Verilator: VHDL vector generic actual and default lengths
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- IEEE 1076-2008 6.5.2.2 and 6.5.7.1: a vector generic value must have the
-- declared length, for a default and for an actual.

library ieee;
use ieee.std_logic_1164.all;

entity leaf_bad_default is
  generic (g : std_logic_vector(3 downto 0) := "11");
  port (o : out integer);
end;

architecture rtl of leaf_bad_default is
begin
  o <= 1;
end;

entity leaf_bad_actual is
  generic (g : std_logic_vector(3 downto 0) := "1111");
  port (o : out integer);
end;

architecture rtl of leaf_bad_actual is
begin
  o <= 1;
end;

entity t is
  port (o : out integer);
end;

architecture rtl of t is
begin
  u1 : entity work.leaf_bad_default port map (o => open);
  u2 : entity work.leaf_bad_actual generic map (g => "11") port map (o => open);
end;
