-- DESCRIPTION: Verilator: VHDL logical operators mixing a vector and a scalar bit
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    slv   : in  std_logic_vector(3 downto 0);
    s     : in  std_logic;
    bv    : in  bit_vector(3 downto 0);
    b     : in  bit;
    qand  : out std_logic_vector(3 downto 0);
    qor   : out std_logic_vector(3 downto 0);
    qnand : out std_logic_vector(3 downto 0);
    qleft : out std_logic_vector(3 downto 0);
    qb    : out bit_vector(3 downto 0)
  );
end t;

architecture behav of t is
begin
  -- IEEE 1164 (and the predefined operators for bit/bit_vector) apply the scalar to
  -- every element, in either operand order.
  qand  <= slv and s;
  qor   <= slv or s;
  qnand <= slv nand s;
  qleft <= s and slv;
  qb    <= bv and b;
end behav;
