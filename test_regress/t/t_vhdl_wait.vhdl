-- DESCRIPTION: Verilator: VHDL wait-until clock processes
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in std_logic;
    d   : in std_logic;
    qp  : out std_logic;
    qf  : out std_logic;
    qe  : out std_logic;
    qb  : out std_logic;
    qv  : out std_logic
  );
end;

architecture rtl of t is
begin
  process is
  begin
    wait until rising_edge(clk);
    qp <= d;
  end process;

  process is
  begin
    wait until falling_edge(clk);
    qf <= d;
  end process;

  process is
  begin
    wait until clk'event and clk = '1';
    qe <= d;
  end process;

  -- A process loops forever: the pre-wait statement runs again after the body, so the
  -- last assignment of the iteration is the one before the next wait
  process is
  begin
    qb <= '0';
    wait until rising_edge(clk);
    qb <= '1';
  end process;

  process is
    variable v : std_logic := '0';
  begin
    v := '1';
    wait until rising_edge(clk);
    v := d;
    qv <= v;
  end process;
end;
