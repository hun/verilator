-- DESCRIPTION: Verilator: VHDL a signal read in the time-zero activation keeps its old value
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in  std_logic;
    b_o : out std_logic
  );
end;

architecture rtl of t is
  signal aa : std_logic := '0';
  signal bb : std_logic := '0';
begin
  -- The time-zero activation is a normal one: a signal assignment updates the
  -- signal only at the end of the delta (IEEE 1076-2008 10.5), so the read of
  -- aa after assigning it still sees the old value. GHDL: b_o is '0' after
  -- the first activation and '1' after the first clock edge.
  process (clk)
  begin
    aa <= '1';
    bb <= aa;
  end process;

  b_o <= bb;
end;
