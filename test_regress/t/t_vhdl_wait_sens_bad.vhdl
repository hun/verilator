-- DESCRIPTION: Verilator: VHDL wait inside a sensitized process is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A process cannot have both a sensitivity list and a wait statement (IEEE
-- 1076-2008 10.2); GHDL refuses this design.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in  std_logic;
    y   : out std_logic
  );
end;

architecture rtl of t is
begin
  proc : process (clk)
  begin
    y <= clk;
    wait until rising_edge(clk);
  end process;
end;
