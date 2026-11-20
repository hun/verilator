-- DESCRIPTION: Verilator: VHDL constant from a pure function using math_real
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.math_real.all;

package size_pkg is
  function ceil_log2(value : positive) return natural;
  function bits_for(value : positive) return natural;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.math_real.all;

package body size_pkg is
  function ceil_log2(value : positive) return natural is
  begin
    -- A real round trip: the result only exists at elaboration
    return natural(ceil(log2(real(value))));
  end function;

  function bits_for(value : positive) return natural is
  begin
    return ceil_log2(value + 1);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.size_pkg.all;

entity t is
  port (
    i_a    : in  std_logic;
    o_log  : out natural range 0 to 7;
    o_bits : out natural range 0 to 7;
    o_sum  : out natural range 0 to 15
    );
end entity;

library ieee;
use ieee.std_logic_1164.all;
use work.size_pkg.all;

architecture rtl of t is
  constant depth : natural := ceil_log2(8);
  -- The subtype's range names a constant that only a folded call can value
  subtype level_t is natural range 0 to depth;
  constant extra : natural := bits_for(5);
  signal level : level_t;
begin
  level <= depth when i_a else 0;
  o_log <= level;
  o_bits <= extra when i_a else 0;
  o_sum <= depth + extra when i_a else 0;
end architecture;
