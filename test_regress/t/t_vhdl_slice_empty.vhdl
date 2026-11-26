-- DESCRIPTION: Verilator: a null slice on both sides of an assignment
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A pipeline shift writes `p(2 to N) <= p(1 to N-1)`, which for a one-deep
-- pipeline is the null array `p(2 to 1)` on both sides (IEEE 1076-2008 8.5):
-- the statement is a no-op, not an error. The three-deep copy of the same
-- statement must still shift.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    clk : in  std_logic;
    di  : in  std_logic_vector(7 downto 0);
    o1  : out std_logic_vector(7 downto 0);
    o3  : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
  type one_t is array (1 to 1) of std_logic_vector(7 downto 0);
  type three_t is array (1 to 3) of std_logic_vector(7 downto 0);
  signal p1 : one_t := (others => (others => '0'));
  signal p3 : three_t := (others => (others => '0'));
begin
  process (clk)
  begin
    if rising_edge(clk) then
      p1(1)     <= di;
      p1(2 to 1) <= p1(1 to 0);

      p3(1)      <= di;
      p3(2 to 3) <= p3(1 to 2);
    end if;
  end process;

  o1 <= p1(1);
  o3 <= p3(3);
end architecture;
