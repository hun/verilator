-- DESCRIPTION: Verilator: a record member constrained down to its elements
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
        z : out unsigned(7 downto 0));
end entity t;

architecture rtl of t is
  type a_t is record
    value : unsigned;
  end record;
  type a_array_t is array (natural range <>) of a_t;
  type b_t is record
    a : a_array_t;
  end record;
  type b_array_t is array (natural range <>) of b_t;

  -- A member's range is followed by the constraint of its elements
  signal s2 : b_t(a(0 to 1)(value(7 downto 0)));
  signal s3 : b_array_t(0 to 1)(a(0 to 1)(value(7 downto 0)));
begin
  s2.a(0).value <= x;
  s2.a(1).value <= x + 1;
  s3(0).a(0).value <= s2.a(0).value;
  s3(1).a(1).value <= s2.a(1).value + 1;
  y <= s3(0).a(0).value;
  z <= s3(1).a(1).value;
end architecture rtl;
