-- DESCRIPTION: Verilator: an array actual that a call returns
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (o : out bit_vector(3 downto 0));
end entity t;

architecture rtl of t is
  type a_t is record
    value : bit_vector;
  end record;
  type a_array_t is array (natural range <>) of a_t;
  type b_t is record
    a : a_array_t;
  end record;
  type b_array_t is array (natural range <>) of b_t;
  type table_t is array (natural range <>, natural range <>) of a_t;

  function make_b return b_array_t is
    variable ret : b_array_t(0 to 0)(a(0 to 1)(value(3 downto 0)));
  begin
    ret(0).a(1).value := "1010";
    return ret;
  end function;

  -- The array grows by one element per call and is passed on as the formal of the next, so
  -- the shape of an actual that a call returns has to reach the formal
  function grow(prefix : b_array_t; n : natural) return b_array_t is
  begin
    if n >= 2 then
      return prefix;
    end if;
    return grow(prefix & make_b, n + 1);
  end function;

  function build return b_array_t is
    variable empty : b_array_t(0 to -1)(a(0 to 1)(value(3 downto 0)));
  begin
    return grow(empty, 0);
  end function;

  function shape(b : b_array_t) return table_t is
    variable ret : table_t(0 to b'length - 1, 0 to b(0).a'length - 1)(value(3 downto 0));
  begin
    return ret;
  end function;

  constant b : b_array_t := build;
  constant table : table_t := shape(b);
begin
  o <= b(1).a(1).value;
end architecture rtl;
