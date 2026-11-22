-- DESCRIPTION: Verilator: VHDL asynchronous reset
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
    clk : in std_logic;
    rst : in std_logic;
    d   : in std_logic;
    q   : out std_logic
  );
end;

architecture rtl of t is
begin
  process (clk, rst) is
  begin
    if rst then
      q <= '1';
    elsif rising_edge(clk) then
      q <= d;
    end if;
  end process;
end;
