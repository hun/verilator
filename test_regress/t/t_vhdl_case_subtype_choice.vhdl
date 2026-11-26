-- DESCRIPTION: Verilator: a subtype name as a case choice
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
  port (x : in unsigned(3 downto 0);
        y : out unsigned(1 downto 0);
        z : out unsigned(1 downto 0));
end entity t;

architecture rtl of t is
  type color is (red, green, blue, white);
  subtype small_t is integer range 0 to 3;
  subtype big_t   is integer range 8 to 11;
  subtype warm_t  is color range red to green;
  signal c : color;
begin
  c <= color'val(to_integer(x(1 downto 0)));

  process (x)
  begin
    case to_integer(x) is
      when small_t => y <= "01";
      when big_t   => y <= "10";
      when others  => y <= "00";
    end case;
  end process;

  process (c)
  begin
    case c is
      when warm_t => z <= "01";
      when others => z <= "10";
    end case;
  end process;
end architecture rtl;
