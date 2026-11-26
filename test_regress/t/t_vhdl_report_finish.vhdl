-- DESCRIPTION: Verilator: VHDL computed report with severity failure ends the run
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A report whose message is not a literal must still execute, including the
-- `severity failure` termination (IEEE 1076-2008 10.4 only asks an implementation to continue after NOTE, WARNING or
-- ERROR). GHDL stops on the same
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
