-- DESCRIPTION: Verilator: VHDL case generate
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
library ieee;
use ieee.std_logic_1164.all;

entity t is
  generic (
    K : integer := 5;
    M : integer := 9
  );
  port (
    a : in std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0);
    z : out std_logic_vector(3 downto 0);
    w : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- K = 5 is in the range alternative
  g1 : case K generate
    when 0 =>
      y <= a;
    when 1 to 3 =>
      y <= not a;
    when 4 | 5 | 6 =>
      signal tmp : std_logic_vector(3 downto 0);
    begin
      tmp <= a;
      y <= tmp(2 downto 0) & '1';
    when others =>
      y <= (others => '0');
  end generate g1;

  -- M = 9 matches nothing and there is no others, so z has no driver here
  g2 : case M generate
    when 1 | 2 =>
      z <= a;
  end generate g2;
  z <= not a when M = 9 else a;

  g3 : case K + 1 generate
    when alt : 6 =>
      w <= a;
    when others =>
      w <= (others => '1');
  end generate g3;
end;
