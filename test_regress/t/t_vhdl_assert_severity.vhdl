-- DESCRIPTION: Verilator: VHDL assert severity decides whether the run continues
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `severity failure` ends the run, the other severities report and the process goes on
-- (IEEE 1076-2008 10.3 only asks an implementation to continue after NOTE, WARNING or
-- ERROR). The failing assertions below are severity error and warning, so
-- the counter keeps counting and the model reaches the end of the trace.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_assert_severity is
  port (
    clk : in  std_logic;
    d   : in  std_logic;
    q   : out std_logic;
    n   : out integer
  );
end entity;

architecture rtl of t_vhdl_assert_severity is
  signal c : integer := 0;
begin
  n <= c;

  process (clk)
  begin
    if rising_edge(clk) then
      c <= c + 1;
      q <= d;
      -- A passing assertion says nothing
      assert (d = '1') report "d must be one" severity error;
      -- This one fails on every clock, and the run still counts to the end
      assert (c = 0) report "counting" severity warning;
    end if;
  end process;
end architecture;
