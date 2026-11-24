-- DESCRIPTION: Verilator: a std_logic added to or taken from a numeric vector
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
  port (u : in unsigned(3 downto 0);
        s : in signed(3 downto 0);
        b : in std_logic;
        up : out unsigned(3 downto 0);
        um : out unsigned(3 downto 0);
        sp : out signed(3 downto 0);
        sm : out signed(3 downto 0));
end entity t;

architecture rtl of t is
begin
  -- The bit is the number 0 or 1, not a sign: signed '1' + s is s + 1
  up <= u + b;
  um <= u - b;
  sp <= b + s;
  sm <= s - b;
end architecture rtl;
