-- DESCRIPTION: Verilator: VHDL package signal declared beside a deferred constant
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The constant's call has its body in the package body, so the specification is emitted
-- a second time to complete the constant. The signal must still be declared once.

library ieee;
use ieee.std_logic_1164.all;

package t_vhdl_pkg_signal_defer_pkg is
  function twice(x : integer) return integer;
  constant C : integer := twice(3);
  signal s : std_logic := '1';
end package;

package body t_vhdl_pkg_signal_defer_pkg is
  function twice(x : integer) return integer is
  begin
    return 2 * x;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.t_vhdl_pkg_signal_defer_pkg.all;

entity t_vhdl_pkg_signal_defer is
end entity;

architecture sim of t_vhdl_pkg_signal_defer is
begin
  check : process
  begin
    wait for 1 ns;
    assert C = 6 report "deferred constant" severity failure;
    assert s = '1' report "signal initial value" severity failure;
    report "all checks passed";
    std.env.finish;
    wait;
  end process;
end architecture;
