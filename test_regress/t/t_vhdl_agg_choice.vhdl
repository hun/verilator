-- DESCRIPTION: Verilator: an aggregate whose choice range names part of its bounds
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
end entity t;

architecture sim of t is
  subtype t_word is unsigned(7 downto 0);

  signal a : unsigned(7 downto 0);
  signal b : unsigned(7 downto 0);
begin
  -- CHECK values: a choice range covers only its own span, so the aggregate is that
  -- many elements wide and the concatenation supplies the rest of the word
  a <= (t_word'left downto 4 => '1') & unsigned'("0011");
  b <= (t_word'right to 3 => '1') & unsigned'("1010");

  check : process
  begin
    wait for 1 ns;
    if a = "11110011" then
      report "a is set";
    else
      report "a is wrong";
    end if;
    if b = "11111010" then
      report "b is set";
    else
      report "b is wrong";
    end if;
    std.env.finish;
    wait;
  end process check;
end architecture sim;
