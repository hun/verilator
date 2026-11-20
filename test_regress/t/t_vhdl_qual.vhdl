-- DESCRIPTION: Verilator: VHDL qualified aggregates
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
    a : in std_logic_vector(3 downto 0);
    b : in std_logic_vector(3 downto 0);
    y : out std_logic_vector(3 downto 0);
    z : out std_logic_vector(3 downto 0);
    w : out std_logic_vector(3 downto 0);
    qc : out std_logic;
    qn : out std_logic_vector(3 downto 0);
    qe : out integer
  );
end;

architecture rtl of t is
  -- Qualification operands that are not aggregates (IEEE 1800-2023 9.3.5): the
  -- token after `'('` may be a character, string or general expression.
  constant c_bit : std_logic := std_logic'('1');
  constant c_vec : unsigned(3 downto 0) := unsigned'("1010");
  constant c_sum : integer := integer'(2 + 1);
begin
  -- A one-element qualification, whose bounds come from the choices
  y <= std_logic_vector(unsigned(a) + unsigned'(0 => b(0)));
  -- The leftmost index is the most significant element of an aggregate
  z <= std_logic_vector(unsigned'(1 => a(1), 0 => a(0)));
  -- A range choice fixes the length
  w <= std_logic_vector(unsigned'(1 to 2 => '1'));
  qc <= c_bit;
  qn <= std_logic_vector(c_vec);
  qe <= c_sum;
end;
