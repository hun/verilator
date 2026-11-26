-- DESCRIPTION: Verilator: std_match with one argument has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- std_match takes two operands.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable a : bit;
  begin
    if std_match(a) then o <= '1'; end if;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
