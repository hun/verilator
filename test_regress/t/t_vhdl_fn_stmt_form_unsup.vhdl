-- DESCRIPTION: Verilator: a signal assignment inside a function body is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A function body cannot assign a signal.
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, s : in bit;
    y : out bit
  );
end entity t;

architecture rtl of t is
  signal z : bit;
  function f(x : bit) return bit is
  begin
    z <= x;
    return x;
  end function;
begin
  y <= f('1');
end architecture rtl;
