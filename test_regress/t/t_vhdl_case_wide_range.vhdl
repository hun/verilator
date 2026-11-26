-- DESCRIPTION: Verilator: a case choice range too wide to expand
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.numeric_std.all;

entity t is
  port (x : in signed(31 downto 0);
        y : out signed(7 downto 0));
end entity t;

architecture rtl of t is
  subtype high_t is integer range 100 to integer'high;
  subtype low_t  is integer range integer'low to -100;
begin
  process (x)
  begin
    case to_integer(x) is
      when high_t => y <= to_signed(1, 8);
      when low_t  => y <= to_signed(-1, 8);
      when 0 to 9 => y <= to_signed(5, 8);
      when 10     => y <= to_signed(6, 8);
      when others => y <= to_signed(0, 8);
    end case;
  end process;
end architecture rtl;
