-- DESCRIPTION: Verilator: VHDL severity failure ends the run at the assertion
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
    done_o : out std_logic
  );
end;

architecture rtl of t is
begin
  -- Severity failure ends the simulation at that point (IEEE 1076-2008 8.3):
  -- no later statement of the process runs. GHDL reports "first" once and
  -- done_o stays '0'.
  proc : process
  begin
    assert false report "first" severity failure;
    done_o <= '1';
  end process;
end;
