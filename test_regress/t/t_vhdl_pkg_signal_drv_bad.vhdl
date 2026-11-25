-- DESCRIPTION: Verilator: VHDL unresolved package signal driven from two units
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An unresolved signal may have only one driver (IEEE 1076-2008 6.4.2.3), and a package
-- signal is one signal however many units drive it, so two entities that each drive it
-- are two drivers.

package t_vhdl_pkg_signal_drv_bad_pkg is
  signal tally : integer := 0;
end package;

library ieee;
use work.t_vhdl_pkg_signal_drv_bad_pkg.all;

entity t_vhdl_pkg_signal_drv_bad_a is
end entity;

architecture a of t_vhdl_pkg_signal_drv_bad_a is
begin
  tally <= 1;
end architecture;

library ieee;
use work.t_vhdl_pkg_signal_drv_bad_pkg.all;

entity t_vhdl_pkg_signal_drv_bad_b is
end entity;

architecture b of t_vhdl_pkg_signal_drv_bad_b is
begin
  tally <= 2;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_pkg_signal_drv_bad is
end entity;

architecture top of t_vhdl_pkg_signal_drv_bad is
begin
  u_a : entity work.t_vhdl_pkg_signal_drv_bad_a;
  u_b : entity work.t_vhdl_pkg_signal_drv_bad_b;
end architecture;
