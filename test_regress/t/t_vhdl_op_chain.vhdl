-- DESCRIPTION: Verilator: VHDL operator chains pass with parentheses
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- IEEE 1076-2008 9.1: parentheses make a non-associative operator a primary,
-- so each of these forms is legal.

library ieee;
use ieee.std_logic_1164.all;

entity tb is
end;

architecture sim of tb is
  signal a : std_logic_vector(3 downto 0) := "1000";
  signal b : boolean := false;
begin
  process
  begin
    b <= (1 = 1) = true;
    wait for 1 ns;
    assert b report "parenthesized relation" severity failure;
    assert ((a sll 1) sll 2) = "0000" report "parenthesized shift" severity failure;
    assert (not (not a(0))) = '0' report "parenthesized unary" severity failure;
    std.env.finish;
    wait;
  end process;
end;
