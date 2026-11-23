-- DESCRIPTION: Verilator: VHDL fixed-point diagnostics
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.fixed_pkg.all;

entity t is
  port (
    sa : in sfixed(3 downto -2);
    ua : in ufixed(3 downto -2);
    c : in std_logic;
    n : in natural;
    v : in std_logic_vector(7 downto 0);
    y : out sfixed(4 downto -2);
    z : out ufixed(4 downto -2);
    r : out sfixed(3 downto -2)
  );
end entity;

architecture rtl of t is
  signal ascending : sfixed(-2 to 3);
begin
  y <= sa + ua;
  r <= sa + sa;
  y <= sa + v;
  z <= -ua;
  r <= resize(sa, n, 0);
  r <= resize(sa, -2, 3);
  r <= to_sfixed(n, -2, 3);
  r <= to_sfixed(v, n);
  r <= to_sfixed(v, 3, -2);
  r <= to_sfixed(c, 3, -2);
  z <= to_ufixed(n, 4, -2) + to_ufixed(to_signed(ua, 4), 3, -2);
  r <= to_sfixed(to_signed(sa, n), 3, -2);
  r <= to_sfixed(to_unsigned(sa, 4), 3, -2);
end architecture;
