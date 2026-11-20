-- DESCRIPTION: Verilator: VHDL attributes of the predefined integer types
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.math_real.all;

package bits_pkg is
  function bits return positive;
  constant width_c : positive;
  constant low_c : integer;
  constant nat_low_c : natural;
  constant i_right_c : integer;
  constant nat_left_c : natural;
end package bits_pkg;

package body bits_pkg is
  function bits return positive is
  begin
    -- A power of two, so the conversion is exact whether `real` is a 32-bit float or a
    -- double; the bounds of POSITIVE are read inside the body of a pure function
    return integer(log2(real(1024 * positive'low))) + 1;
  end function bits;

  -- The function has no formal, so it may be named without parentheses
  constant width_c : positive := bits;
  constant low_c : integer := integer'low;
  constant nat_low_c : natural := natural'low;
  constant i_right_c : integer := integer'right;
  constant nat_left_c : natural := natural'left;
end package body bits_pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.bits_pkg.all;

entity t is
  port (
    i_delta : in  integer;
    o_bits  : out integer;
    o_high  : out integer;
    o_low   : out integer;
    o_nat   : out integer;
    o_right : out integer;
    o_nat_l : out integer
  );
end entity t;

architecture rtl of t is
  constant high_c : integer := integer'high;
begin
  o_bits <= width_c;
  -- The high bound is read through a port, so the caller cannot fold it away
  o_high <= high_c - i_delta;
  o_low <= low_c;
  o_nat <= nat_low_c;
  o_right <= i_right_c;
  o_nat_l <= nat_left_c;
end architecture rtl;
