-- DESCRIPTION: Verilator: VHDL an external name of a signal in an instance
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_external_name_sub is
  port (clk   : in  std_logic;
        rst   : in  std_logic;
        a     : in  std_logic_vector(7 downto 0);
        total : out std_logic_vector(7 downto 0));
end t_vhdl_external_name_sub;

architecture behav of t_vhdl_external_name_sub is
  signal accum : std_logic_vector(7 downto 0);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        accum <= (others => '0');
      else
        accum <= std_logic_vector (unsigned (accum) + unsigned (a));
      end if;
    end if;
  end process;
  total <= accum;
end behav;

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_external_name is
  port (clk   : in  std_logic;
        rst   : in  std_logic;
        a     : in  std_logic_vector(7 downto 0);
        rel   : out std_logic_vector(7 downto 0);
        abs_q : out std_logic_vector(7 downto 0);
        als   : out std_logic_vector(7 downto 0));
end t_vhdl_external_name;

architecture behav of t_vhdl_external_name is
  alias dut_accum is << signal dut.accum : std_logic_vector(7 downto 0) >>;
begin
  dut : entity work.t_vhdl_external_name_sub
    port map (clk => clk, rst => rst, a => a, total => open);
  -- The signal is not a port of dut: it is reached through the hierarchy
  rel <= << signal dut.accum : std_logic_vector(7 downto 0) >>;
  process (all)
  begin
    als <= dut_accum;
  end process;
  abs_q <= << signal .t_vhdl_external_name.dut.accum : std_logic_vector(7 downto 0) >>;
end behav;
