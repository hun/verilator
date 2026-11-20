-- DESCRIPTION: Verilator: a static condition folds a pure function of constants
--
-- An elaboration-time check such as `assert is_power_of_two(len)` folds to a constant,
-- which keeps the callee's body out of the lowering: the body of a math helper reaches
-- for `real(...)`, which has no synthesizable form.
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

package mathish_pkg is
  function ceil_log2(value : positive) return natural;
  function floor_log2(value : positive) return natural;
  function log2(value : positive) return natural;
  function is_power_of_two(value : positive) return boolean;
end package mathish_pkg;

package body mathish_pkg is
  function ceil_log2(value : positive) return natural is
  begin
    return natural(ceil(log2(real(value))));
  end function;

  function floor_log2(value : positive) return natural is
  begin
    return natural(log2(real(value)));
  end function;

  function log2(value : positive) return natural is
  begin
    assert is_power_of_two(value) report "Must be power of two";
    return floor_log2(value);
  end function;

  -- The constant in this body is why the call cannot be inlined: it has to fold
  function is_power_of_two(value : positive) return boolean is
    constant log2_value : natural := floor_log2(value);
  begin
    return 2 ** log2_value = value;
  end function;
end package body mathish_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.mathish_pkg.all;

entity t is
  generic (len : positive := 16);
  port (o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  constant c_ceil : natural := ceil_log2(len);
begin
  assert is_power_of_two(len) report "len must be a power of two" severity failure;
  assert not is_power_of_two(12) report "12 must not be a power of two" severity failure;

  g_pow : if is_power_of_two(8) generate
    o <= std_logic_vector(to_unsigned(c_ceil * 16 + 1, 8));
  else generate
    o <= (others => '0');
  end generate g_pow;
end architecture rtl;
