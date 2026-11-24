-- DESCRIPTION: Verilator: a constant whose array type takes its shape from a call
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
  port (x : in unsigned(7 downto 0);
        y : out unsigned(7 downto 0);
        z : out std_logic);
end entity t;

architecture rtl of t is
  type entry_t is record
    a : std_logic;
    b : unsigned(7 downto 0);
  end record;
  type table_t is array (natural range <>, natural range <>) of entry_t;
  type bits_t is array (natural range <>, natural range <>) of std_logic;

  function make_table return table_t is
    variable ret : table_t(0 to 1, 0 to 2);
  begin
    for i in 0 to 1 loop
      for j in 0 to 2 loop
        ret(i, j).a := '1';
        ret(i, j).b := to_unsigned(i * 16 + j, 8);
      end loop;
    end loop;
    return ret;
  end function;

  function make_bits return bits_t is
    variable ret : bits_t(0 to 3, 0 to 3) := (others => (others => '0'));
  begin
    ret(2, 3) := '1';
    return ret;
  end function;

  -- The element type has no width of its own, so the call's result gives the shape
  constant table : table_t := make_table;
  constant bits : bits_t := make_bits;
begin
  y <= table(1, 2).b + x;
  z <= bits(2, 3) and table(0, 0).a;
end architecture rtl;
