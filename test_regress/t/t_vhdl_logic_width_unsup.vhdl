-- DESCRIPTION: Verilator: a logical operator on vectors of different widths has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Both operands of and must have the same length.


entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable a : bit_vector(3 downto 0); variable b : bit_vector(7 downto 0); variable r : bit_vector(3 downto 0);
  begin
    r := a and b;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
