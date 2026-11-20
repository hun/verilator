-- DESCRIPTION: Verilator: a constant expression with real operands folds at elaboration
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
  port (
    o_div : out std_logic_vector(27 downto 0);
    o_rnd : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
  constant c_ns   : natural := 125;
  constant c_freq : natural := 100000000;
  -- 1e9 / 125 / 1e8 * 268435456 is 21474836.48, so the integer conversion truncates
  -- to 21474836. Evaluating the real expression as integers would divide 8e6 by 1e8
  -- first and give zero.
  constant c_div : std_logic_vector(27 downto 0) :=
    std_logic_vector(to_unsigned(integer(1000000000.0 / real(c_ns) / real(c_freq) * 268435456.0), 28));
  -- A conversion to an integer type rounds to nearest: 2.7 becomes 3
  constant c_rnd : integer := integer(2.7);
begin
  o_div <= c_div;
  o_rnd <= std_logic_vector(to_unsigned(c_rnd, 4));
end architecture rtl;