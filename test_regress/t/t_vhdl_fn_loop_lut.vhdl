-- DESCRIPTION: Verilator: a function filling a table of more entries than a loop unrolls
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The 16384 sin() entries fold while the function runs; the sampled values are GHDL's

library ieee;
use ieee.math_real.all;
entity t is end entity;
architecture a of t is
  constant POINTS : positive := 16384;
  constant MAX_AMP : integer := 2047;
  type lut_t is array (natural range <>) of integer range -MAX_AMP to MAX_AMP;
  function gen return lut_t is
    variable r : lut_t(0 to POINTS - 1) := (others => 0);
    constant step : real := (360.0 / real(r'length)) * MATH_DEG_TO_RAD;
    variable x : real := 0.0;
  begin
    for i in r'range loop
      r(i) := integer(sin(x) * real(MAX_AMP));
      x := x + step;
    end loop;
    return r;
  end function;
  constant lut : lut_t := gen;
begin
  process begin
    wait for 1 ns;
    assert lut(0) = 0 and lut(1) = 1 and lut(1638) = 1203 report "low" severity failure;
    assert lut(4096) = 2047 and lut(11264) = -1891 and lut(16383) = -1 report "high" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
