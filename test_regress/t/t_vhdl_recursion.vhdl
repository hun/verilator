-- DESCRIPTION: Verilator: a function that calls itself
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_misc.all;
use ieee.numeric_std.all;

entity t is
  port (i : in std_logic_vector(15 downto 0);
        o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  -- The length of the argument ends the recursion, so each call is inlined down to one
  function onehot(v : std_logic_vector; low : natural := 0) return natural is
    constant mid : natural := v'length / 2;
    variable upper : std_logic_vector(v'length - mid - 1 downto 0) := v(v'length - 1 downto mid);
    variable lower : std_logic_vector(mid - 1 downto 0) := v(mid - 1 downto 0);
  begin
    if v'length = 1 then
      return low;
    elsif or_reduce(upper) = '1' then
      return onehot(upper, low + mid);
    else
      return onehot(lower, low);
    end if;
  end function;

  function fact(n : natural) return natural is
  begin
    if n <= 1 then
      return 1;
    end if;
    return n * fact(n - 1);
  end function;

  constant f5 : natural := fact(5);
begin
  o(3 downto 0) <= std_logic_vector(to_unsigned(onehot(i), 4));
  o(4) <= '1' when f5 = 120 else '0';
  o(7 downto 5) <= "000";
end architecture rtl;
