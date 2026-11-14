-- DESCRIPTION: Verilator: a bit-string literal whose length truncates its value
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o : out std_logic_vector(1 downto 0)
  );
end entity t;

architecture rtl of t is
  -- 2 bits cannot hold the value 7 of the octal digit, so the length would drop set bits
  signal narrow : std_logic_vector(1 downto 0) := 2o"7";
begin
  o <= narrow;
end architecture rtl;
