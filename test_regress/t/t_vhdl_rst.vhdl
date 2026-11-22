-- DESCRIPTION: Verilator: VHDL async reset and integer counter
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
    q   : out integer range 0 to 3
  );
end;

architecture rtl of t is
  signal cnt : integer range 0 to 3 := 0;
begin
  process (clk, rst) is
  begin
    if rst = '1' then
      cnt <= 0;
    elsif rising_edge(clk) then
      if cnt = 3 then
        cnt <= 0;
      else
        cnt <= cnt + 1;
      end if;
    end if;
  end process;
  q <= cnt;
end;
