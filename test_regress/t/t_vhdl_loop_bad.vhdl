-- DESCRIPTION: Verilator: a loop that only a run-time value ends
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (x : in std_logic_vector(3 downto 0);
        o : out integer range 0 to 7);
end entity t;

architecture rtl of t is
  -- Only the bits of x end this loop, so nothing folds and there is no count to unroll to
  function first(v : std_logic_vector) return integer is
    variable i : natural := 0;
  begin
    loop
      exit when v(i mod 4) = '1';
      i := i + 1;
    end loop;
    return i;
  end function;
begin
  o <= first(x);
end architecture rtl;
