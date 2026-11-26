-- DESCRIPTION: Verilator: a math_real function with two arguments has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- sin takes one argument.

library ieee;
use ieee.math_real.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable x : real; variable r : real;
  begin
    r := sin(x, x);
    o <= '1';
    wait;
  end process p;
end architecture rtl;
