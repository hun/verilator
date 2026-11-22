-- DESCRIPTION: Verilator: VHDL if/elsif/else
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
    a : in std_logic;
    b : in std_logic;
    y : out std_logic
  );
end;

architecture rtl of t is
begin
  process (a, b) is
  begin
    if a = '1' then
      y <= b;
    elsif b = '1' then
      y <= '0';
    else
      y <= '1';
    end if;
  end process;
end;
