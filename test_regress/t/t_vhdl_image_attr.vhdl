-- DESCRIPTION: Verilator: the image attribute of a type with an argument
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  generic (count : natural := 42);
  port (o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  -- The value as written: the decimal of an integer, the literal of a Boolean value, as
  -- a string constant that takes its bounds from the value, IEEE 1076-2008 16.2.2
  constant num_text : string := natural'image(count);
  constant flag_text : string := boolean'image(false);
begin
  o <= x"5A" when (num_text = "42") and (flag_text = "false") else x"A5";
end architecture rtl;
