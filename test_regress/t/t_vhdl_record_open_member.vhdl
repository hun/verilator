-- DESCRIPTION: Verilator: a record member left open in a constraint
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (x : in std_logic_vector(3 downto 0);
        y : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
  type rec_t is record
    a : std_logic_vector;
    b : std_logic_vector;
  end record;

  -- Member a keeps its own range open; only b is constrained
  signal s : rec_t(a(open), b(3 downto 0));
begin
  s.b <= x;
  y <= s.b;
end architecture rtl;
