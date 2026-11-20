-- DESCRIPTION: Verilator: a bit-string literal whose length truncates its value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
