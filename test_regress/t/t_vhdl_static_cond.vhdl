-- DESCRIPTION: Verilator: a static condition folds a pure function of constants
--
-- An elaboration-time check such as `assert is_pow2(len)` folds to a constant,
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

package lg_pkg is
  function ceil_lg(n : positive) return natural;
  function floor_lg(n : positive) return natural;
  function log2(n : positive) return natural;
  function is_pow2(n : positive) return boolean;
end package lg_pkg;

package body lg_pkg is
  function ceil_lg(n : positive) return natural is
  begin
    return natural(ceil(log2(real(n))));
  end function;

  function floor_lg(n : positive) return natural is
  begin
    return natural(log2(real(n)));
  end function;

  function log2(n : positive) return natural is
  begin
    assert is_pow2(n) report "Must be power of two";
    return floor_lg(n);
  end function;

  -- The constant in this body is why the call cannot be inlined: it has to fold
  function is_pow2(n : positive) return boolean is
    constant lg_val : natural := floor_lg(n);
  begin
    return 2 ** lg_val = n;
  end function;
end package body lg_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.lg_pkg.all;

entity t is
  generic (len : positive := 16);
  port (o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  constant c_ceil : natural := ceil_lg(len);
begin
  assert is_pow2(len) report "len must be a power of two" severity failure;
  assert not is_pow2(12) report "12 must not be a power of two" severity failure;

  g_pow : if is_pow2(8) generate
    o <= std_logic_vector(to_unsigned(c_ceil * 16 + 1, 8));
  else generate
    o <= (others => '0');
  end generate g_pow;
end architecture rtl;
