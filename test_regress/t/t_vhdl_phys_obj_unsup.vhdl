-- DESCRIPTION: Verilator: VHDL run-time physical objects are refused
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- The subset has no run-time physical object (5.14): a physical value exists
-- only while elaborating.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
begin
  proc : process
    variable d : time;
  begin
    d := 10 ns;
    wait;
  end process;
end;
