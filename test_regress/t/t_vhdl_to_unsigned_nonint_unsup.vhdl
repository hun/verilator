-- DESCRIPTION: Verilator: to_unsigned of a bit_vector has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- to_unsigned takes a natural operand.

library ieee;
use ieee.numeric_std.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable bv : bit_vector(1 downto 0); variable n : natural; variable r : unsigned(3 downto 0);
  begin
    r := to_unsigned(bv, n);
    o <= '1';
    wait;
  end process p;
end architecture rtl;
