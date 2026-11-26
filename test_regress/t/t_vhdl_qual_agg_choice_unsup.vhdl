-- DESCRIPTION: Verilator: a qualified aggregate with a run-time choice has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The choices of a bit_vector qualified aggregate must be static.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  p : process
    variable i : natural := 1; variable v : bit_vector(3 downto 0);
  begin
    v := bit_vector'(i => '1', others => '0');
    o <= '1';
    wait;
  end process p;
end architecture rtl;
