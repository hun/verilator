-- DESCRIPTION: Verilator: a return without a value in a function is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A return in a function must give a value (IEEE 1076-2008 10.13).
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, s : in bit;
    y : out bit_vector(0 to 3)
  );
end entity t;

architecture rtl of t is
  function f(x : bit) return bit is
  begin
    for i in 0 to 1 loop
      null;
    end loop;
    if x = '1' then
      return;
    end if;
    return x;
  end function;
begin
  y(0) <= f(s);
end architecture rtl;
