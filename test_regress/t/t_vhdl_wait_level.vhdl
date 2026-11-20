-- DESCRIPTION: Verilator: VHDL level-gated clock waits, one-shot and looping
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
    clk     : in  std_logic;
    en      : in  std_logic;
    once_o  : out integer;
    count_o : out integer
  );
end entity t;

architecture rtl of t is
  signal once_r  : integer := 0;
  signal count_r : integer := 0;
begin

  -- Runs on the first rising edge with en high, then suspends forever
  one_shot : process
    variable v : integer := 0;
  begin
    wait until en = '1' and rising_edge(clk);
    v := 7;
    once_r <= v;
    wait;
  end process;

  -- Runs on every rising edge with en high
  looping : process
  begin
    wait until en = '1' and rising_edge(clk);
    count_r <= count_r + 1;
  end process;

  once_o  <= once_r;
  count_o <= count_r;

end architecture rtl;
