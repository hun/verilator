-- DESCRIPTION: Verilator: a VHDL wait for a delay computed at run time
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A delay that reads a signal or a variable is a count of femtoseconds at run time, so it
-- must scale to the same simulated time as a literal delay.

entity t_vhdl_wait_runtime is
end entity t_vhdl_wait_runtime;

architecture sim of t_vhdl_wait_runtime is
  signal n : integer := 0;
begin
  stim : process
    variable k : integer := 2;
  begin
    n <= 3;
    wait for 1 ns;
    -- 1 ns + 3 * 10 ns
    wait for n * 10 ns;
    -- 31 ns + 2 * 1 ns + 2 ns
    wait for k * 1 ns + 2 ns;
    -- 35 ns + 4 ns / 2
    wait for 4 ns / k;
    report "run-time delays done";
    std.env.finish;
    wait;
  end process stim;
end architecture sim;
