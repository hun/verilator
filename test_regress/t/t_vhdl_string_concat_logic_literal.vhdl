-- DESCRIPTION: Verilator: a string literal of logic characters beside a string in a concatenation
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  generic (g : natural := 7);
  port (o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  -- "X" is a legal std_logic_vector, but beside a string it is a string of one character
  -- (IEEE 1076-2008 9.3.2, 9.2.5)
  constant image_text : string := "X" & integer'image(g);
  constant to_string_text : string := "Z" & to_string(g);
  constant plain_text : string := "R" & integer'image(g);
begin
  o(0) <= '1' when image_text = "X7" else '0';
  o(1) <= '1' when to_string_text = "Z7" else '0';
  o(2) <= '1' when plain_text = "R7" else '0';
  o(3) <= '1' when image_text'length = 2 else '0';
  o(4) <= '1' when to_string_text'length = 2 else '0';
  o(5) <= '1' when ("U" & integer'image(g)) = "U7" else '0';
  o(6) <= '1' when (integer'image(g) & "-") = "7-" else '0';
  o(7) <= '1' when (to_string(g) & "1") = "71" else '0';
end architecture rtl;
