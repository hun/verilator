-- DESCRIPTION: Verilator: an unconstrained multi-dimensional port is refused cleanly
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package multidim_pkg is
  type arr2d is array (integer range <>, integer range <>) of bit;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.multidim_pkg.all;

entity t is
  port (
    c : in  arr2d;
    o : out bit
  );
end entity;

architecture rtl of t is
begin
  o <= '0';
end architecture;
