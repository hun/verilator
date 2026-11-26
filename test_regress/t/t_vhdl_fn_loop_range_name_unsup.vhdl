-- DESCRIPTION: Verilator: a function loop over the range of a call result is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The range attribute needs an array object or type as prefix (IEEE 1076-2008 16.2.3).
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, s : in bit;
    y : out bit_vector(0 to 3)
  );
end entity t;

architecture rtl of t is
  function g(x : bit) return bit_vector is
  begin
    return "0101";
  end function;
  function f(x : bit) return bit is
  begin
    for i in g(x)'range loop
      null;
    end loop;
    return x;
  end function;
begin
  y(0) <= f('1');
end architecture rtl;
