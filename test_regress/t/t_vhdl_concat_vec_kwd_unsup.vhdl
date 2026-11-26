-- DESCRIPTION: Verilator: concatenation of bit_vector and std_logic_vector has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The two vector element types differ.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable bv : bit_vector(1 downto 0); variable lv : std_logic_vector(1 downto 0); variable r : bit_vector(3 downto 0);
  begin
    r := bv & lv;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
