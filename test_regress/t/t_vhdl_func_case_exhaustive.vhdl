-- DESCRIPTION: Verilator: an inlined function with an exhaustive case returns on every path
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Every value of the three-bit selector is covered by a returning case arm, so
-- no `others` (and no code after the case) is needed (IEEE 1076-2008 10.9).

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    v  : in  std_logic_vector(2 downto 0);
    p  : out std_logic;
    e  : out std_logic
  );
end entity;

architecture rtl of t is
  function parity(sel : std_logic_vector(2 downto 0)) return std_logic is
  begin
    case sel is
      when "000" => return '0';
      when "001" => return '1';
      when "010" => return '1';
      when "011" => return '0';
      when "100" => return '1';
      when "101" => return '0';
      when "110" => return '0';
      when "111" => return '1';
    end case;
  end function;

  function is_even(sel : std_logic_vector(2 downto 0)) return std_logic is
  begin
    if sel(0) = '1' then
      case sel(2 downto 1) is
        when "00" => return '0';
        when "01" => return '1';
        when "10" => return '0';
        when others => return '1';
      end case;
    else
      return '1';
    end if;
  end function;
begin
  p <= parity(v);
  e <= is_even(v);
end architecture;
