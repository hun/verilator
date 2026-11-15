-- DESCRIPTION: Verilator: VHDL a signal assignment with a zero delay
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `x <= value after delay` with a delay of zero is a delta: the assignment behaves
-- like an un-delayed one, which is how a gate-level netlist writes it. The delay is
-- a generic here, so the same source covers both an un-delayed and a delayed
-- assignment.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_after is
  generic (
    delay : time := 0 ns
  );
  port (
    clk : in  std_logic;
    d   : in  std_logic;
    q   : out std_logic;
    r   : out std_logic
  );
end entity;

architecture rtl of t_vhdl_after is
begin
  -- A concurrent assignment may state a delay; zero of them changes nothing
  q <= d after delay;

  regs : process (clk)
  begin
    if rising_edge(clk) then
      r <= d after delay;
    end if;
  end process;
end architecture;