-- DESCRIPTION: Verilator: ext and sxt of std_logic_arith
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;

entity t is
  port (
    v  : in  std_logic_vector(3 downto 0);
    ez : out std_logic_vector(7 downto 0);
    es : out std_logic_vector(7 downto 0);
    tz : out std_logic_vector(1 downto 0);
    ts : out std_logic_vector(1 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  ez <= ext(v, 8);
  es <= sxt(v, 8);
  -- A narrower size keeps the low elements
  tz <= ext(v, 2);
  ts <= sxt(v, 2);
end architecture rtl;
