-- DESCRIPTION: Verilator: VHDL wait inside a sensitized process is refused
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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
