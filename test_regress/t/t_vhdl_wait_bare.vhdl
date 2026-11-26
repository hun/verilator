-- DESCRIPTION: Verilator: VHDL a bare wait suspends the process for good
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk   : in  std_logic;
    cnt_o : out integer
  );
end;

architecture rtl of t is
begin
  -- A wait with no sensitivity, condition or timeout suspends forever
  -- (IEEE 1076-2008 10.2): after the third edge the process must not run
  -- again, whatever the clock does. GHDL: the count stops at 3.
  proc : process
    variable c : integer := 0;
  begin
    wait until rising_edge(clk);
    c := c + 1;
    cnt_o <= c;
    if c = 3 then
      wait;
    end if;
  end process;
end;
