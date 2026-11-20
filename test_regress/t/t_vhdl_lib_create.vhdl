-- DESCRIPTION: Verilator: VHDL counter built as a DPI library with --lib-create
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dut is
  port (
    clk : in std_logic;
    q   : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of dut is
  signal s : unsigned(3 downto 0) := (others => '0');
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      s <= s + 1;
    end if;
  end process;
  q <= std_logic_vector(s);
end;
