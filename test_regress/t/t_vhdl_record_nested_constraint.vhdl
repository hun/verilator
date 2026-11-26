-- DESCRIPTION: Verilator: a record constraint that reaches into a record member
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (x : in std_logic_vector(7 downto 0);
        y : out std_logic_vector(7 downto 0);
        z : out std_logic_vector(1 downto 0);
        w : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
  type inner_t is record
    f1 : std_logic_vector;
    f2 : std_logic_vector;
  end record;
  type outer_t is record
    a : std_logic_vector;
    r : inner_t;
  end record;

  -- Each member of the inner record gets its own range
  signal s : outer_t(a(3 downto 0), r(f1(7 downto 0), f2(1 downto 0)));
begin
  s.a <= x(7 downto 4);
  s.r.f1 <= x;
  s.r.f2 <= x(1 downto 0);
  y <= s.r.f1;
  z <= s.r.f2;
  w <= s.a;
end architecture rtl;
