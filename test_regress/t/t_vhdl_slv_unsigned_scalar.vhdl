-- DESCRIPTION: Verilator: VHDL std_logic_unsigned arithmetic with a std_logic operand
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity t is
  port (
    a : out std_logic_vector(3 downto 0);  -- 15 + '1' wraps to 0
    b : out std_logic_vector(3 downto 0);  -- 0 - '1' wraps to 15
    c : out std_logic_vector(3 downto 0);  -- '1' + 6
    d : out std_logic_vector(3 downto 0);  -- 9 + '0'
    e : out std_logic_vector(3 downto 0)   -- variable counted down twice
  );
end;

architecture rtl of t is
  constant k15 : std_logic_vector(3 downto 0) := "1111";
  constant k0  : std_logic_vector(3 downto 0) := "0000";
  constant k6  : std_logic_vector(3 downto 0) := "0110";
  constant k9  : std_logic_vector(3 downto 0) := "1001";
  signal one   : std_logic := '1';
begin
  a <= k15 + one;
  b <= k0 - one;
  c <= one + k6;
  d <= k9 + '0';
  process (one)
    variable v : std_logic_vector(3 downto 0);
  begin
    v := k6;
    v := v - '1';
    v := v - one;
    e <= v;
  end process;
end;
