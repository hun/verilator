-- DESCRIPTION: Verilator: the std_logic_1164 X01 conversions
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o_x01 : out std_logic_vector(3 downto 0);
    o_01  : out std_logic_vector(3 downto 0);
    o_z   : out std_logic_vector(3 downto 0);
    o_ux  : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
  signal a   : std_logic_vector(3 downto 0) := "1010";
  signal b   : std_logic_vector(3 downto 0) := "0110";
begin
  -- The model has no metavalues, so these conversions keep the value
  o_x01 <= to_x01(a);
  o_01  <= to_01(b);
  o_z   <= to_x01z(b);
  o_ux  <= to_ux01(a);
end architecture rtl;