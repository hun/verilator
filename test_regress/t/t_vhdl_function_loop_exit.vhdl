-- DESCRIPTION: Verilator: loops in a function body that end on a condition
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (v : in std_logic_vector(3 downto 0);
        a : out integer range 0 to 7;
        b : out integer range 0 to 7;
        c : out integer range 0 to 7);
end entity t;

architecture rtl of t is
  -- Each finds the index of the lowest set bit, or 4 when none is set. The loops below
  -- unroll: the counter is static, only the bit tested is not.
  function ffs_for(x : std_logic_vector) return integer is
    variable idx : integer := 0;
  begin
    for i in x'reverse_range loop
      exit when x(i) = '1';
      idx := idx + 1;
    end loop;
    return idx;
  end function;

  -- A bare loop ends on a static exit or a data-dependent one
  function ffs_loop(x : std_logic_vector) return integer is
    variable idx : integer range 0 to 4 := 0;
  begin
    loop
      exit when idx = 4;
      exit when x(idx) = '1';
      idx := idx + 1;
    end loop;
    return idx;
  end function;

  -- The left operand of and ends the loop before x(4) is read
  function ffs_while(x : std_logic_vector) return integer is
    variable idx : integer range 0 to 4 := 0;
  begin
    while idx < 4 and x(idx) = '0' loop
      idx := idx + 1;
    end loop;
    return idx;
  end function;
begin
  a <= ffs_for(v);
  b <= ffs_loop(v);
  c <= ffs_while(v);
end architecture rtl;
