-- DESCRIPTION: Verilator: VHDL std_logic_signed arithmetic and comparison on std_logic_vector
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_signed.all;

entity t is
  port (
    a  : in  std_logic_vector(3 downto 0);
    b  : in  std_logic_vector(2 downto 0);
    s5 : out std_logic_vector(3 downto 0);
    s6 : out std_logic_vector(3 downto 0);
    m  : out std_logic_vector(6 downto 0);
    i1 : out std_logic_vector(3 downto 0);
    i2 : out std_logic_vector(3 downto 0);
    r  : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
begin
  s5 <= a + b;
  s6 <= b - a;
  m  <= a * b;
  i1 <= a + 3;
  i2 <= a - 5;
  r(0) <= '1' when a < b else '0';
  r(1) <= '1' when a <= b else '0';
  r(2) <= '1' when a > b else '0';
  r(3) <= '1' when a >= b else '0';
  r(4) <= '1' when a = b else '0';
  r(5) <= '1' when a /= b else '0';
  r(6) <= '1' when a > 2 else '0';
  r(7) <= '1' when 1 < a else '0';
end;
