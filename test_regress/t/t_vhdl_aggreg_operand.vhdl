-- DESCRIPTION: Verilator: an aggregate as an operator operand takes its type from its choices
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    o_cat   : out unsigned(3 downto 0);
    o_named : out unsigned(3 downto 0);
    o_int   : out integer
  );
end entity t;

architecture rtl of t is
  signal cat   : unsigned(3 downto 0);
  signal named : unsigned(3 downto 0);
begin

  -- The operand of `&` has no target type: its choice range and its element give the width
  cat <= '1' & (0 to 2 => '0');

  -- With a target type the aggregate keeps the usual meaning
  named <= (3 => '1', others => '0');

  o_cat   <= cat;
  o_named <= named;
  o_int   <= to_integer(cat);

end architecture rtl;
