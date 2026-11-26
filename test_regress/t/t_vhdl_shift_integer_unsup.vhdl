-- DESCRIPTION: Verilator: a shift of an integer has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- sll is defined for array operands.


entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable i : integer; variable r : integer;
  begin
    r := i sll 2;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
