-- DESCRIPTION: Verilator: VHDL discrete subtypes as ranges, std_match, and variable slice bounds
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
    a  : in  std_logic_vector(7 downto 0);
    m0 : out std_logic;
    m1 : out std_logic;
    r  : out std_logic_vector(15 downto 0);
    w  : out integer
  );
end entity t;

architecture rtl of t is
  constant N : natural := 4;
  -- An index constraint that is a discrete subtype rather than a range
  subtype idx_t is integer range 0 to N - 1;
  type arr_t is array (idx_t) of std_logic_vector(7 downto 0);
  signal mem : arr_t;
begin
  mem(0) <= a;

  -- A '-' bit matches anything, so this compares the high nibble only
  m0 <= '1' when std_match(a, "1010----") else '0';
  m1 <= '1' when std_match(a, "10101010") else '0';

  -- A loop range that is a discrete subtype
  count : process
    variable cnt : integer;
  begin
    cnt := 0;
    for i in idx_t loop
      cnt := cnt + 1;
    end loop;
    w <= cnt;
    wait;
  end process;

  -- Slice bounds that are variables holding static values
  split : process (a)
    variable v    : std_logic_vector(15 downto 0);
    variable lbit : integer;
  begin
    v := (others => '0');
    lbit := 0;
    v(lbit + 7 downto lbit) := a;
    lbit := 8;
    v(lbit + 7 downto lbit) := not a;
    r <= v;
  end process;
end architecture rtl;
