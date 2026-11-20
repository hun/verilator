-- DESCRIPTION: Verilator: VHDL computed report with severity failure ends the run
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A report whose message is not a literal must still execute, including the
-- `severity failure` termination (IEEE 1076-2008 8.3). GHDL stops on the same
-- source; the harness checks that the model saw a finish.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_report_finish is
  port (
    clk : in  std_logic;
    q   : out std_logic
  );
end entity;

architecture rtl of t_vhdl_report_finish is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      q <= '1';
      report "fatal " & std_logic'image(clk) severity failure;
    end if;
  end process;
end architecture;
