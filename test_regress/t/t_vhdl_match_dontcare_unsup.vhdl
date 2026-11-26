-- DESCRIPTION: Verilator: a matching relational operator with a dash in a literal has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The 2-state model cannot express the dash in a matching comparison.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable a : std_logic_vector(1 downto 0);
  begin
    if "1-" ?< a then o <= '1'; end if;
    o <= '1';
    wait;
  end process p;
end architecture rtl;
