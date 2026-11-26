-- DESCRIPTION: Verilator: an integer choice range on a selector that is not an integer
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.numeric_std.all;

entity t is
  port (s : in unsigned(19 downto 0);
        y : out bit);
end entity t;

architecture rtl of t is
begin
  process (s)
  begin
    case s is
      when 0 to 100000 => y <= '1';
      when others => y <= '0';
    end case;
  end process;
end architecture rtl;
