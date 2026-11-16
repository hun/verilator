-- DESCRIPTION: Verilator: an alias of a signal in a sensitivity list and an edge call
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    d   : in std_logic;
    q   : out std_logic
  );
end entity t;

architecture rtl of t is
  -- The process is sensitive to the alias and the register is clocked by the edge
  -- function on it, both of which denote clk
  alias fast_clk : std_logic is clk;
begin
  process (fast_clk) is
  begin
    if rising_edge(fast_clk) then
      q <= d;
    end if;
  end process;
end architecture rtl;