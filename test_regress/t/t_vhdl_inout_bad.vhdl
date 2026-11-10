-- DESCRIPTION: Verilator: VHDL procedural assignment to an inout port
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity top_inout is
  port (
    clk : in std_logic;
    d : in std_logic;
    q : inout std_logic
  );
end;

architecture rtl of top_inout is
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      -- Procedurally driving an inout port is not modelled
      q <= d;
    end if;
  end process;
end;
