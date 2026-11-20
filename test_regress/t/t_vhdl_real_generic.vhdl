-- DESCRIPTION: Verilator: a real generic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A generic of type real is an elaboration-time value: it folds through the constant
-- folders (arithmetic, round, min and a conversion to integer), so no floating type
-- reaches the model. The same holds for the predefined ieee.math_real functions and
-- for a package function called with a qualified name, as open-logic's strobe
-- generator calls work.olo_base_pkg_math.min.

library ieee;
use ieee.std_logic_1164.all;
use ieee.math_real.all;

package pk is
  function pick(a : integer; b : integer) return integer;
end package;

package body pk is
  function pick(a : integer; b : integer) return integer is
  begin
    if a > b then
      return b;
    else
      return a;
    end if;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.math_real.all;

entity real_gen is
  generic (FREQ_g : real := 100.0; DIV_g : real := 3.0);
  port (
    n : out integer;
    m : out integer
  );
end entity;

architecture rtl of real_gen is
  constant Ratio_c : real := FREQ_g / DIV_g;
  constant Count_c : integer := integer(round(Ratio_c * 100.0));
  constant Small_c : integer := integer(min(round(Ratio_c * 100.0), real(1000)));
  constant Pick_c : integer := work.pk.pick(Count_c, 2000);
begin
  assert FREQ_g / DIV_g <= 1.0e6 report "ratio too large" severity failure;
  n <= Count_c;
  m <= Small_c + Pick_c;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    n1 : out integer;
    m1 : out integer;
    n2 : out integer;
    m2 : out integer
  );
end entity;

architecture rtl of t is
begin
  u_explicit : entity work.real_gen
    generic map (FREQ_g => 50.0, DIV_g => 4.0)
    port map (n => n1, m => m1);

  u_default : entity work.real_gen
    port map (n => n2, m => m2);
end architecture;
