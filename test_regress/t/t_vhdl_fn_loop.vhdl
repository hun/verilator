-- DESCRIPTION: Verilator: calls folded to constants in slice bounds and exponents
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package p is
  function log2_f(input : in natural) return natural;
  function index_size_f(input : in natural) return natural;
end package p;

package body p is
  -- A while loop: the trip count follows from the argument
  function log2_f(input : in natural) return natural is
    variable retval : natural := 0;
    variable temp   : natural := input;
  begin
    while temp > 1 loop
      retval := retval + 1;
      temp := temp / 2;
    end loop;
    return retval;
  end function log2_f;

  -- A range that is huge but for a body that returns as soon as it has the answer
  function index_size_f(input : in natural) return natural is
  begin
    for i in 0 to natural'high loop
      if 2**i >= input then
        return i;
      end if;
    end loop;
    return 0;
  end function index_size_f;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.p.all;

entity t is
  port (
    r_log2 : out std_logic_vector(7 downto 0);  -- log2_f(1024)
    r_pow  : out std_logic_vector(7 downto 0);  -- 2 ** log2_f(64)
    r_idx  : out std_logic_vector(7 downto 0);  -- index_size_f(1000)
    r_bits : out std_logic_vector(9 downto 0);  -- v(hi_c downto 0)
    r_zero : out std_logic_vector(7 downto 0)   -- log2_f(1), no trip at all
  );
end entity t;

architecture rtl of t is
  constant hi_c : natural := index_size_f(1024) - 1;
  signal v : std_logic_vector(15 downto 0) := x"beef";
begin
  r_log2 <= std_logic_vector(to_unsigned(log2_f(1024), 8));
  -- The exponent is only known once the call is folded
  r_pow <= std_logic_vector(to_unsigned(2 ** log2_f(64), 8));
  r_idx <= std_logic_vector(to_unsigned(index_size_f(1000), 8));
  -- Both bounds of the slice need the folded call
  r_bits <= v(hi_c downto 0);
  r_zero <= std_logic_vector(to_unsigned(log2_f(1), 8));
end architecture rtl;
