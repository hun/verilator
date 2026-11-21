-- DESCRIPTION: Verilator: VHDL failing severity failure assertion fails the run
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A passing assertion continues; a failing `severity failure` assertion stops the
-- simulation with a non-zero exit status, so a failing testbench fails its run.

entity t_vhdl_severity_exit is
end entity t_vhdl_severity_exit;

architecture sim of t_vhdl_severity_exit is
  signal x : integer := 1;
begin
  check : process
  begin
    wait for 1 ns;
    assert x = 1 report "x is not 1" severity failure;
    report "passed the first check";
    assert x = 2 report "x is not 2" severity failure;
    report "after the failing check";
    std.env.finish;
    wait;
  end process check;
end architecture sim;
