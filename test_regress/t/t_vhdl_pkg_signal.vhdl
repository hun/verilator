-- DESCRIPTION: Verilator: VHDL package signals written and read across entities
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A package signal is one signal every unit using the package reads and drives. The
-- writer drives it from its process, the top reads it through a use clause, an alias
-- and a qualified name, and drives one itself.

library ieee;
use ieee.std_logic_1164.all;

package t_vhdl_pkg_signal_pkg is
  type glob_t is record
    tck : std_logic;
    sel : std_logic_vector(3 downto 0);
  end record;
  signal glob : glob_t;
  signal pulse : std_logic := '0';
  signal count : integer := 5;
  signal flag : integer := 0;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.t_vhdl_pkg_signal_pkg.all;

entity t_vhdl_pkg_signal_wr is
end entity;

architecture a of t_vhdl_pkg_signal_wr is
begin
  drive : process
  begin
    glob.tck <= '0';
    glob.sel <= "0010";
    wait for 1 ns;
    glob.tck <= '1';
    pulse <= '1';
    count <= count + 1;
    wait;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.t_vhdl_pkg_signal_pkg.all;

entity t_vhdl_pkg_signal is
end entity;

architecture sim of t_vhdl_pkg_signal is
  alias j : glob_t is glob;
begin
  u_wr : entity work.t_vhdl_pkg_signal_wr;

  check : process
  begin
    work.t_vhdl_pkg_signal_pkg.flag <= 7;
    wait for 2 ns;
    assert j.tck = '1' report "alias read of glob.tck" severity failure;
    assert glob.sel = "0010" report "record read of glob.sel" severity failure;
    assert pulse = '1' report "pulse not driven" severity failure;
    assert count = 6 report "count not incremented" severity failure;
    assert work.t_vhdl_pkg_signal_pkg.count = 6 report "qualified read" severity failure;
    assert flag = 7 report "qualified write" severity failure;
    report "all checks passed";
    std.env.finish;
    wait;
  end process;
end architecture;
