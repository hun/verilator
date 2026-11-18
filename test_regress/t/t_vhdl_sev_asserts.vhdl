-- DESCRIPTION: Verilator: statements after consecutive severity-failure asserts still run
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- Two failing-severity assertions in a row gate every later statement of the
-- process on both conditions (IEEE 1076-2008 8.3). Each gate must come from the
-- assertion it follows, not from the wrapper the previous one left behind.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk : in  std_logic;
    o   : out std_logic_vector(1 downto 0)
  );
end;

architecture rtl of t is
  signal cnt : std_logic_vector(1 downto 0);
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      assert not is_X(cnt) severity failure;
      assert not is_X(cnt) severity failure;
      cnt <= std_logic_vector(unsigned(cnt) + 1);
    end if;
  end process;

  o <= cnt;
end;
