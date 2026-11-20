-- DESCRIPTION: Verilator: an asynchronous reset flip-flop clears without a clock edge
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
    clk   : in  std_logic;
    rstn  : in  std_logic;
    cnt_o : out unsigned(3 downto 0)
  );
end entity t;

architecture rtl of t is
  signal cnt : unsigned(3 downto 0);
begin
  process (rstn, clk) is
  begin
    if (rstn = '0') then
      cnt <= (others => '0');
    elsif rising_edge(clk) then
      cnt <= cnt + 1;
    end if;
  end process;
  cnt_o <= cnt;
end architecture rtl;
