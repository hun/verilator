-- DESCRIPTION: Verilator: statements after consecutive severity-failure asserts still run
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
