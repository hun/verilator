-- DESCRIPTION: Verilator: VHDL real generic/constant comparisons
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- real is handled at elaboration only: a real generic/constant and real
-- comparisons must fold to static values that drive ordinary std_logic.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_real is
  generic (V : real := 1.0);
  port (
    lt   : out std_logic;
    le   : out std_logic;
    eq   : out std_logic;
    ge   : out std_logic;
    gt   : out std_logic;
    bits : out std_logic_vector(3 downto 0)
  );
end entity;

architecture a of t_vhdl_real is
  constant D : real := 0.25;
  constant N : integer := integer(round(V / D));  -- 1.0 / 0.25 = 4
begin
  lt   <= '1' when V <  1.5 else '0';
  le   <= '1' when V <= 1.5 else '0';
  eq   <= '1' when V =  1.5 else '0';
  ge   <= '1' when V >= 1.5 else '0';
  gt   <= '1' when V >  1.5 else '0';
  bits <= std_logic_vector(to_unsigned(N, 4));
end architecture;
