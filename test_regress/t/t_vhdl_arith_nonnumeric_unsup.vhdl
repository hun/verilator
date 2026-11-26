-- DESCRIPTION: Verilator: arithmetic between std_logic_vector and bit_vector has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The operands of + are not both numeric.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable a : std_logic_vector(3 downto 0); variable b : bit_vector(3 downto 0); variable r : std_logic_vector(3 downto 0);
  begin
    r := a + b;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
