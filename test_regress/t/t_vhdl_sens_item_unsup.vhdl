-- DESCRIPTION: Verilator: VHDL sensitivity items must be bare signal names
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A sensitivity element is a signal name (IEEE 1076-2008 11.3); an element of
-- one would need per-element events the model does not have, so it is refused
-- rather than silently widened to process(all).

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in  std_logic_vector(1 downto 0);
    y   : out std_logic
  );
end;

architecture rtl of t is
begin
  proc : process (clk(0))
  begin
    y <= clk(0);
  end process;
end;
