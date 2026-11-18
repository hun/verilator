-- DESCRIPTION: Verilator: VHDL a loop range is evaluated once, at loop entry
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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
