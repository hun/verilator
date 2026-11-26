-- DESCRIPTION: Verilator: a function returning values of different lengths is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An unconstrained function result takes its length from the first return.
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    s : in bit;
    s2 : in bit_vector(0 to 1);
    s4 : in bit_vector(0 to 3);
    y : out bit_vector(0 to 3)
  );
end entity t;

architecture rtl of t is
  function f(x : bit; p : bit_vector; q : bit_vector) return bit_vector is
  begin
    for i in 0 to 1 loop
      null;
    end loop;
    if x = '1' then
      return p;
    end if;
    return q;
  end function;
begin
  y <= f(s, s2, s4);
end architecture rtl;
