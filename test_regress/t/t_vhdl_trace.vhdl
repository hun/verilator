-- DESCRIPTION: Verilator: VHDL signals, variables and ports are traced
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_trace is
  port (
    clk : in  std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end entity t_vhdl_trace;

architecture rtl of t_vhdl_trace is
  type hist_t is array (0 to 1) of unsigned(3 downto 0);
  signal count : unsigned(3 downto 0) := (others => '0');
  signal hist  : hist_t := (others => (others => '0'));
begin
  process (clk)
    variable next_count : unsigned(3 downto 0);
  begin
    if rising_edge(clk) then
      next_count := count + 1;
      count <= next_count;
      hist <= (next_count, hist(0));
    end if;
  end process;
  q <= std_logic_vector(count);
end architecture rtl;
