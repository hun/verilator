-- DESCRIPTION: Verilator: a case on std_logic whose choices share a value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
entity t is
  port (d : in std_logic_vector(2 downto 0); q : out std_logic_vector(2 downto 0));
end entity;
architecture a of t is
  function to01 (inp : in std_logic) return std_logic is
  begin
    case inp is
      when '0' | 'L' => return '0';
      when '1' | 'H' => return '1';
      when others => return '0';
    end case;
  end function;
begin
  process(d)
    variable r : std_logic_vector(2 downto 0);
  begin
    for i in 0 to 2 loop r(i) := to01(d(i)); end loop;
    q <= r;
  end process;
end architecture;