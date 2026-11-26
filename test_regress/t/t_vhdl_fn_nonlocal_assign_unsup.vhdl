-- DESCRIPTION: Verilator: a function assigning a variable outside its body is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A function body may only assign its own locals.
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, s : in bit;
    y : out bit_vector(0 to 3)
  );
end entity t;

architecture rtl of t is
  shared variable g : bit;
  function f(x : bit) return bit is
  begin
    g := x;
    return g;
  end function;
begin
  y(0) <= f(s);
end architecture rtl;
