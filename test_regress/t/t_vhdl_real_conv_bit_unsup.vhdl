-- DESCRIPTION: Verilator: real conversion of a bit has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The real conversion takes an integer or real operand.


entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable b : bit; variable r : real;
  begin
    r := real(b);
    o <= '1';
    wait;
  end process p;
end architecture rtl;
