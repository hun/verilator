-- DESCRIPTION: Verilator: VHDL fixed-point unsupported diagnostics
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.fixed_pkg.all;

entity t is
  port (
    sa : in sfixed(3 downto -2);
    n : in natural;
    y : out sfixed(6 downto -6);
    r : out sfixed(3 downto -2)
  );
end entity;

architecture rtl of t is
  signal open_range : sfixed;
begin
  y <= sa / sa;
  r <= not sa;
  r <= to_sfixed(1.5 * real(n), 3, -2);
end architecture;
