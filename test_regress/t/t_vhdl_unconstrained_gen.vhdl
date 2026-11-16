-- DESCRIPTION: Verilator: an unconstrained vector constant and generic take their width
--              from the literal that defaults them
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  generic (
    -- The mark leaves the width open; the literal fixes it at 5
    C_pixclk_div : std_logic_vector := "00111"
  );
  port (
    o : out std_logic_vector(4 downto 0)
  );
end entity t;

architecture rtl of t is
  constant c_cmd : std_logic_vector := "00101";
begin
  o <= C_pixclk_div and c_cmd;
end architecture rtl;