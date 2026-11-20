-- DESCRIPTION: Verilator: a conversion to an unconstrained multi-dimensional array is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package conv_pkg is
  type arr2d is array (natural range <>, natural range <>) of bit;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.conv_pkg.all;

entity t is
  port (
    a : in  bit_vector(3 downto 0);
    q : out bit_vector(3 downto 0)
  );
end entity;

architecture rtl of t is
  signal s  : arr2d(0 to 1, 0 to 1);
  signal s1 : arr2d(0 to 1, 0 to 1);
begin
  s  <= ((a(3), a(2)), (a(1), a(0)));
  s1 <= arr2d(s);
  q  <= (s1(0, 0), s1(1, 0), s1(0, 1), s1(1, 1));
end architecture;
