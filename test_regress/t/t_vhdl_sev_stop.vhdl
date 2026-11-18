-- DESCRIPTION: Verilator: VHDL severity failure ends the run at the assertion
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
  -- Severity failure ends the simulation at that point (IEEE 1076-2008 8.3):
  -- no later statement of the process runs. GHDL reports "first" once and
  -- done_o stays '0'.
  proc : process
  begin
    assert false report "first" severity failure;
    done_o <= '1';
  end process;
end;
