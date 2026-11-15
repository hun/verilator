-- DESCRIPTION: Verilator: a signal assignment with a nonzero delay
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `x <= value after 5 ns` inside a process needs a scheduled update, which a cycle
-- model has no notion of; a delay of zero is a delta and is accepted (t_vhdl_after).

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_delay_unsup is
  port (
    clk : in  std_logic;
    d   : in  std_logic;
    q   : out std_logic
  );
end entity;

architecture rtl of t_vhdl_delay_unsup is
begin
  regs : process (clk)
  begin
    if rising_edge(clk) then
      q <= d after 5 ns;
    end if;
  end process;
end architecture;
