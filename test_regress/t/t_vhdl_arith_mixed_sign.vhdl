-- DESCRIPTION: Verilator: unsigned and signed operands under std_logic_arith
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
  port (u : in std_logic_vector(3 downto 0);
        s : in std_logic_vector(3 downto 0);
        sum : out std_logic_vector(4 downto 0);
        diff : out std_logic_vector(4 downto 0);
        wide : out std_logic_vector(5 downto 0));
end entity t;

architecture rtl of t is
  signal uu : unsigned(3 downto 0);
  signal ss : signed(3 downto 0);
begin
  uu <= unsigned(u);
  ss <= signed(s);
  -- The sum of an unsigned and a signed operand is signed, one bit wider than the
  -- unsigned operand when that is the wider one
  sum <= std_logic_vector(uu + ss);
  diff <= std_logic_vector(uu - ss);
  wide <= std_logic_vector(conv_unsigned(uu + ss, 6));
end architecture rtl;
