-- DESCRIPTION: Verilator: a matching case with don't-care choices
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_case_matching is
  port (i : in std_logic_vector(2 downto 0);
        o : out std_logic_vector(1 downto 0));
end;

architecture r of t_vhdl_case_matching is
begin
  process (i)
  begin
    case? i is
      when "000"       => o <= "00";
      when "001"       => o <= "01";
      when "01-"       => o <= "10";
      when "1-0" | "1-1" => o <= "11";
      when others      => o <= "00";
    end case?;
  end process;
end;
