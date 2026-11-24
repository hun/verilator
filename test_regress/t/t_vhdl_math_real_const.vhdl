-- DESCRIPTION: Verilator: math_real functions and constants in a static integer expression
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity t is
  port (o : out std_logic_vector(7 downto 0);
        w : out std_logic_vector(15 downto 0));
end entity t;

architecture rtl of t is
  -- Real values have no form in the model, so each expression folds as an integer
  constant bits : integer := integer(ceil(log2(100.0)));
  constant pi100 : integer := integer(floor(MATH_PI * 100.0));
  constant root : integer := integer(floor(log10(sqrt(4096.0 * MATH_SQRT_PI))));
  constant degrees : integer := integer(round(MATH_PI_OVER_2 * MATH_RAD_TO_DEG));
  signal packed : std_logic_vector(15 downto 0)
    := std_logic_vector(to_unsigned(integer(floor(sqrt(4096.0 * MATH_SQRT_PI))), 16));
begin
  o(0) <= '1' when bits = 7 else '0';
  o(1) <= '1' when pi100 = 314 else '0';
  o(2) <= '1' when root = 1 else '0';
  o(3) <= '1' when degrees = 90 else '0';
  o(4) <= '1' when to_integer(unsigned(packed)) = 85 else '0';
  o(7 downto 5) <= "111";
  w <= packed;
end architecture rtl;
