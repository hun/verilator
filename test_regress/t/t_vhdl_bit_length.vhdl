-- DESCRIPTION: Verilator: bit-string literal with an explicit length
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : out std_logic_vector(5 downto 0);
    b : out std_logic_vector(3 downto 0);
    c : out std_logic_vector(7 downto 0);
    d : out std_logic_vector(3 downto 0);
    e : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
  -- The length is the literal's width: 6x"3f" is 011111, not 1111111111
  constant c_a : std_logic_vector(5 downto 0) := 6x"3f";
  constant c_b : std_logic_vector(3 downto 0) := 4x"0";
  constant c_c : std_logic_vector(7 downto 0) := 8x"3f";
  constant c_d : std_logic_vector(3 downto 0) := 4b"1";
  -- Leading zeros of the widest digit may be dropped, so 4o"17" is 1111
  constant c_e : std_logic_vector(3 downto 0) := 4o"17";
begin
  a <= c_a;
  b <= c_b;
  c <= c_c;
  d <= c_d;
  e <= c_e;
end architecture rtl;
