-- DESCRIPTION: Verilator: VHDL a report message of only logic characters still reports
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
  -- A report message is a string of characters, whatever those characters
  -- are (IEEE 1076-2008 8.3): "0101" is a message, not a bit-string literal.
  -- GHDL prints 0101 and ends the run, so done_o never takes its assignment.
  proc : process
  begin
    report "0101" severity failure;
    done_o <= '1';
  end process;
end;
