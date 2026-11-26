-- DESCRIPTION: Verilator: a function alias bounded by a local constant
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (a : in bit_vector(3 downto 0);
        y : out bit_vector(3 downto 0));
end entity t;

architecture rtl of t is
  -- Shift left by count; the alias takes its range from a constant of the function
  function shl (arg : bit_vector; count : natural) return bit_vector is
    constant arg_l : integer := arg'length - 1;
    alias xarg : bit_vector(arg_l downto 0) is arg;
    variable result : bit_vector(arg_l downto 0) := (others => '0');
  begin
    if count <= arg_l then
      result(arg_l downto count) := xarg(arg_l - count downto 0);
    end if;
    return result;
  end function shl;
begin
  y <= shl(a, 1);
end architecture rtl;
