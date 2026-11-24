-- DESCRIPTION: Verilator: a recursion that a run-time value ends
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (x : in std_logic;
        o : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
  -- Only the value of x ends this recursion, which is not known when it is inlined
  function count(x : std_logic; n : natural) return natural is
  begin
    if x = '1' then
      return n;
    end if;
    return count(x, n + 1);
  end function;
begin
  o <= std_logic_vector(to_unsigned(count(x, 0), 4));
end architecture rtl;
