-- DESCRIPTION: Verilator: a non-static exponent that is not a power of two has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Only a base of 2 becomes a shift with a run-time exponent.


entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable i : integer; variable j : integer; variable r : integer;
  begin
    r := i ** j;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
