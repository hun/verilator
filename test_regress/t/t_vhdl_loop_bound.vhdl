-- DESCRIPTION: Verilator: VHDL a loop range is evaluated once, at loop entry
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    n_in : in  integer;
    c_o  : out integer
  );
end;

architecture rtl of t is
begin
  -- The discrete range of a for loop is fixed at loop entry (IEEE 1076-2008
  -- 8.9): the body may assign the bound variable, and the trip count keeps
  -- the value the range had on entry. GHDL: n_in = 3 gives c = 4.
  process (n_in)
    variable n : integer;
    variable c : integer;
  begin
    n := n_in;
    c := 0;
    for i in 0 to n loop
      n := n - 1;
      c := c + 1;
    end loop;
    c_o <= c;
  end process;
end;
