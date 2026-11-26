-- DESCRIPTION: Verilator: concatenation of a vector with an integer expression has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An integer operand is not an element of the vector.


entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable bv : bit_vector(1 downto 0); variable i : integer; variable r : bit_vector(3 downto 0);
  begin
    r := bv & (i + 1);
    o <= '1';
    wait;
  end process p;
end architecture rtl;
