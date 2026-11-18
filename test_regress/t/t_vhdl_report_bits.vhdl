-- DESCRIPTION: Verilator: VHDL a report message of only logic characters still reports
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0
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
