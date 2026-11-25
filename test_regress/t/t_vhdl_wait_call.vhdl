-- DESCRIPTION: Verilator: a procedure with waits, called from a process with waits
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
end entity t;

architecture sim of t is
  signal clk   : std_logic := '0';
  signal req   : std_logic := '0';
  signal rsp   : std_logic := '0';
  signal count : integer := 0;
  signal ticks : integer := 0;
  signal done  : std_logic := '0';

  -- A wait on an edge of a formal, in a procedure called from a process
  procedure TICK (
    signal CLK_S : in std_logic
  ) is
  begin
    wait until rising_edge(CLK_S);
  end procedure;

  -- Waits on levels of formals, in a procedure called from a process
  procedure HANDSHAKE (
    signal REQ_S : in  std_logic;
    signal RSP_S : out std_logic
  ) is
  begin
    wait until REQ_S = '1';
    RSP_S <= '1';
    wait until REQ_S = '0';
    RSP_S <= '0';
  end procedure;
begin
  clk <= not clk after 5 ns;

  tick_p : process
  begin
    for i in 0 to 2 loop
      TICK(clk);
      ticks <= ticks + 1;
    end loop;
    wait;
  end process tick_p;

  drv : process
  begin
    wait for 20 ns;
    for i in 0 to 2 loop
      req <= '1';
      wait until rsp = '1';
      req <= '0';
      wait until rsp = '0';
    end loop;
    wait;
  end process drv;

  main : process
  begin
    for i in 0 to 2 loop
      HANDSHAKE(req, rsp);
      count <= count + 1;
    end loop;
    done <= '1';
    wait;
  end process main;

  check : process
  begin
    wait until done = '1' and ticks = 3;
    assert count = 3 report "count" severity failure;
    report "t_vhdl_wait_call finished";
    std.env.finish;
    wait;
  end process check;
end architecture sim;
