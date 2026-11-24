-- DESCRIPTION: Verilator: a VHDL process with several waits runs them in order
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb is
end entity tb;

architecture sim of tb is
  signal clk  : std_logic := '0';
  signal rst  : std_logic := '1';
  signal en   : std_logic := '0';
  signal cnt  : unsigned(3 downto 0) := (others => '0');
  signal done : std_logic := '0';
  signal seen : std_logic := '0';
begin
  clkgen : process
  begin
    clk <= not clk;
    wait for 5 ns;
  end process clkgen;

  dut : process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        cnt <= (others => '0');
      elsif en = '1' then
        cnt <= cnt + 1;
      end if;
    end if;
  end process dut;

  stim : process
  begin
    -- A timed wait, then clock waits, then a level wait
    wait for 22 ns;
    rst <= '0';
    wait until rising_edge(clk);
    en <= '1';
    wait until rising_edge(clk) and cnt = 2;
    assert cnt = 2 report "count at the compound wait" severity failure;
    en <= '0';
    wait on cnt;
    -- cnt counts once more in the edge that drops en
    wait for 1 ns;
    assert cnt = 3 report "count after en dropped" severity failure;
    seen <= '1';
    wait;
  end process stim;

  check : process
  begin
    wait for 1 ns;
    wait for 80 ns;
    assert seen = '1' report "stim did not pass every wait" severity failure;
    assert cnt = 3 report "cnt is not held" severity failure;
    -- A wait inside a branch and a loop that is unrolled
    for i in 0 to 1 loop
      if cnt = 3 then
        wait for 5 ns;
      end if;
    end loop;
    done <= '1';
    report "t_vhdl_wait_seq finished";
    std.env.finish;
    wait;
  end process check;
end architecture sim;
