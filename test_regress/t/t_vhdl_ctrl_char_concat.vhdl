-- DESCRIPTION: Verilator: control characters in a string concatenation
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
  port (q1  : out std_logic_vector(7 downto 0);
        q2  : out std_logic_vector(7 downto 0);
        q3  : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  constant s : string := "ab" & LF;
  constant z : string := "xy" & NUL;
begin
  q1  <= std_logic_vector(to_unsigned(character'pos(s(3)), 8));
  q2  <= std_logic_vector(to_unsigned(character'pos(z(3)), 8));
  q3  <= std_logic_vector(to_unsigned(s'length + z'length, 8));
end architecture rtl;
