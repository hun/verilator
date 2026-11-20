-- DESCRIPTION: Verilator: VHDL package type range using a package constant
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A record element range names a package integer constant; the package must
-- prescan its integer constants before registering types so the range folds.

package t_vhdl_pkg_range_pkg is
  constant W : integer := 4;
  type rec_t is record
    d : std_logic_vector(W - 1 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.t_vhdl_pkg_range_pkg.all;

entity t_vhdl_pkg_range is
  port (
    a : in  std_logic_vector(3 downto 0);
    o : out std_logic_vector(3 downto 0)
  );
end entity;

architecture a of t_vhdl_pkg_range is
  signal r : rec_t;
begin
  r.d <= a;
  o   <= r.d;
end architecture;
